/**
 * Implementation of the interface to the 'wahern/dns' non-blocking DNS resolver 
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>

#include "dns.h"
#include "port_dns.h"
#include "port_relay_client.h"
#include "wish_connection.h"
#include "wish_connection_mgr.h"
#include "utlist.h"
#include "wish_debug.h"

static struct dns_resolv_conf *resconf(void) {
	static struct dns_resolv_conf *resconf;

	int error;

	if (resconf) {
            return resconf;
        }

	if (!(resconf = dns_resconf_open(&error))) {
            WISHDEBUG(LOG_CRITICAL, "dns_resconf_open: %s", dns_strerror(error));
            abort();
        }

        char *resconf_path = "/etc/resolv.conf";
        error = dns_resconf_loadpath(resconf, resconf_path);

        if (error) {
            WISHDEBUG(LOG_CRITICAL, "%s: %s", resconf_path, dns_strerror(error));
            abort();
	}

        char *nssconf_path = "/etc/nssconf.conf";
	error = dns_nssconf_loadpath(resconf, nssconf_path);
			
        if (error != ENOENT) {
            WISHDEBUG(LOG_CRITICAL, "%s: %s", nssconf_path, dns_strerror(error));
            abort();
	}

	return resconf;
} /* resconf() */


static struct dns_hosts *hosts(void) {
    static struct dns_hosts *hosts;
    int error;

    if (hosts) {
        return hosts;
    }

    /* Explicitly test dns_hosts_local() */
    if (!(hosts = dns_hosts_local(&error))) {
        WISHDEBUG(LOG_CRITICAL, "%s: %s", "/etc/hosts", dns_strerror(error));
        abort();
    }

    return hosts;
} /* hosts() */

static struct dns_cache *cache(void) { return NULL; }

struct dns_buf {
	const unsigned char *base;
	unsigned char *p;
	const unsigned char *pe;
	dns_error_t error;
	size_t overflow;
}; /* struct dns_buf */

size_t dns_rr_addr_print(void *_dst, size_t lim, struct dns_rr *rr, struct dns_packet *P, int *_error) {
	struct dns_buf dst = {_dst, _dst, _dst + lim };
	union dns_any any;
	size_t n;
	int error;

	if (!(n = dns_d_expand(any.ns.host, sizeof any.ns.host, rr->dn.p, P, &error))) {
            goto error;
        }
        
	if ((error = dns_any_parse(dns_any_init(&any, sizeof any), rr, P))) {
            goto error;
        }

	n = dns_any_print(dst.p, dst.pe - dst.p, &any, rr->type);
	dst.p += DNS_PP_MIN(n, (size_t)(dst.pe - dst.p));
        return strlen(dst.base);
        
error:
	*_error = error;

	return 0;
} /* dns_rr_addr_print() */

/** This structure represents an on-going DNS resolving process of wish core.
 * Note that either wish_conn or relay client must always point to NULL */
struct port_dns_resolver {
    struct dns_resolver *R;
    wish_connection_t* wish_conn; //If this is non-NULL then resolving is for a wish connection
    
    wish_relay_client_t *relay_client; //if this is non-NULL then resolving is for a relay client connection
    struct port_dns_resolver *next;
};

static struct port_dns_resolver* resolver_list = NULL;

static struct port_dns_resolver *port_dns_resolver_create(char *qname) {
    const _Bool recurse = false;
    
    struct dns_hints *(*hints)() = (recurse)? &dns_hints_root : &dns_hints_local;
    
    enum dns_type qtype = DNS_T_A;
    int error = 0;

    resconf()->options.recurse = recurse;

    struct dns_resolver *R = NULL;
    
    if (!(R = dns_res_open(resconf(), hosts(), dns_hints_mortal(hints(resconf(), &error)), cache(), dns_opts(), &error))) {
        WISHDEBUG(LOG_CRITICAL, "%s: %s", qname, dns_strerror(error));
        return NULL;
    }

    if ((error = dns_res_submit(R, qname, qtype, DNS_C_IN))) {
        WISHDEBUG(LOG_CRITICAL, "%s: %s", qname, dns_strerror(error));
        return NULL;
    }
    
    struct port_dns_resolver *port_resolver = malloc(sizeof(struct port_dns_resolver));
    if (port_resolver == NULL) {
        WISHDEBUG(LOG_CRITICAL, "%s: %s", qname, "Can't malloc resource");
        return NULL;
    }
    memset(port_resolver, 0, sizeof(struct port_dns_resolver));
    
    port_resolver->R = R;
    return port_resolver;
}

static void port_dns_resolver_signal_error(struct port_dns_resolver *resolver) {
    if (resolver->wish_conn) {
        /* Note: Don't call wish_close_connection() here, as it will do (platform-dependent) things set up by wish_open_connection(), which has not been called in this case. */
        wish_core_t *core = resolver->wish_conn->core;
        if (resolver->wish_conn->context_state == WISH_CONTEXT_IN_MAKING) {
            wish_core_signal_tcp_event(core, resolver->wish_conn, TCP_DISCONNECTED);
        }
        else {
            /** Pre-condition fails. */
            WISHDEBUG(LOG_CRITICAL, "Precondition fails; DNS resolving was in progress and failed, but the wish connection was not under connection phase. Resolver %p conn %p", resolver, resolver->wish_conn);
            abort();
        }
    }
    else if (resolver->relay_client) {
        if (resolver->relay_client->curr_state == WISH_RELAY_CLIENT_RESOLVING) {
            resolver->relay_client->curr_state = WISH_RELAY_CLIENT_WAIT_RECONNECT;
        }
        else {
            /** Pre-condition fails. */
            WISHDEBUG(LOG_CRITICAL, "Precondition fails; DNS resolving was in progress and failed, but the relay client connection was not under connection phase. Resolver %p conn %p", resolver, resolver->relay_client);
            abort();
        }
    }
}

static void port_dns_resolver_delete(struct port_dns_resolver *resolver) {
    dns_res_close(resolver->R);
    LL_DELETE(resolver_list, resolver);
    free(resolver);
}

void port_dns_resolver_cancel_by_wish_connection(wish_connection_t *conn) {
    struct port_dns_resolver *resolver = NULL, *tmp = NULL;
    LL_FOREACH_SAFE(resolver_list, resolver, tmp) {
        if (resolver->wish_conn == conn) {
            assert((resolver->wish_conn != NULL) ^ (resolver->relay_client != NULL));
            port_dns_resolver_delete(resolver);
        }
    }
}

void port_dns_resolver_cancel_by_relay_client(wish_relay_client_t *rc) {
    struct port_dns_resolver *resolver = NULL, *tmp = NULL;
    LL_FOREACH_SAFE(resolver_list, resolver, tmp) {
        if (resolver->relay_client == rc) {
            assert((resolver->wish_conn != NULL) ^ (resolver->relay_client != NULL));
            port_dns_resolver_delete(resolver);
        }
    }
}

int port_dns_start_resolving_wish_conn(wish_connection_t *conn, char *qname) {
    //WISHDEBUG(LOG_CRITICAL, "Starting resolving %s (wish conn)\n", qname);
    struct port_dns_resolver *resolver = port_dns_resolver_create(qname);
    if (resolver != NULL) {
        resolver->wish_conn = conn;
        /* Add the newly allocated resolver to our list of resolvers */
        LL_APPEND(resolver_list, resolver);
    }
    else {
        /* Note: Don't call wish_close_connection() here, as it will do (platform-dependent) things set up by wish_open_connection(), which has not been called in this case. */
        wish_core_t *core = conn->core;
        if (conn->context_state == WISH_CONTEXT_IN_MAKING) {
            WISHDEBUG(LOG_CRITICAL, "Allocation of DNS resolver failed, conn %p", conn);
            wish_core_signal_tcp_event(core, conn, TCP_DISCONNECTED);
        }
        else {
            /* Pre-condition fails  */
            WISHDEBUG(LOG_CRITICAL, "Precondition fails; Allocation of DNS resolver failed, but the wish connection %p was not under connection phase", conn);
            abort();
        }
    }
    
    //WISHDEBUG(LOG_CRITICAL, "Starting resolving %s (resolver %p, wish connection %p)", qname, resolver, conn);
    
    return 0;
}

int port_dns_start_resolving_relay_client(wish_relay_client_t *rc, char *qname) {
    //WISHDEBUG(LOG_CRITICAL, "Starting resolving %s (relay client)\n", qname);
    struct port_dns_resolver *resolver = port_dns_resolver_create(qname);
    if (resolver != NULL) {
        resolver->relay_client = rc;
    
        /* Add the newly allocated resolver to our list of resolvers */
        LL_APPEND(resolver_list, resolver);
    }
    else {
        rc->curr_state = WISH_RELAY_CLIENT_WAIT_RECONNECT;
    }
    
    return 0;
}

int port_dns_poll_resolvers(void) {
    
    /* For each resolver in list of resolvers ... */
    struct port_dns_resolver *resolver = NULL, *tmp = NULL;
    
    LL_FOREACH_SAFE(resolver_list, resolver, tmp) {
        assert((resolver->wish_conn != NULL) ^ (resolver->relay_client != NULL));
        int error = dns_res_check(resolver->R);
    
        if (dns_res_elapsed(resolver->R) > (CONNECTION_DNS_RESOLVE_TIMEOUT)) {
            WISHDEBUG(LOG_CRITICAL, "query timed-out %p", resolver);
            port_dns_resolver_signal_error(resolver);
            port_dns_resolver_delete(resolver);
            continue;
        }
        else if (error == EAGAIN) {
            /* Continue polling */
            /* FIXME: We should add the resolver's filedescriptor to the port's main select() call. 
             * Now we might enter here for no reason at all, and on the other hand we could reduce latency if we immediately know when our DNS request needs handling */
            //dns_res_poll(resolver->R, 1);
        }
        else if (error == 0) {
            /* Query finished */
            
            struct dns_packet *ans;
            ans = dns_res_fetch(resolver->R, &error);
            
            struct dns_rr rr;
            struct dns_rr_i *I = dns_rr_i_new(ans, .sort = 0);
            int len = 0;
            
            enum dns_rcode response_code = dns_p_rcode(ans);
                        
            switch (response_code) {
                case DNS_RC_NOERROR:
                    while (dns_rr_grep(&rr, 1, I, ans, &error)) {
                        if (rr.section == DNS_S_AN && rr.class == DNS_C_IN && rr.type == DNS_T_A) {
                            char pretty[256];
                            if ((len = dns_rr_addr_print(pretty, sizeof pretty, &rr, ans, &error))) {
                                //WISHDEBUG(LOG_CRITICAL, "Resolved to: %s (%p)", pretty, resolver);
                                
                                //Continue opening the connection with new info (connection or relay client)
                                if (resolver->wish_conn) {
                                    /* We were resolving for a normal wish connection */
                                    wish_ip_addr_t ip;

                                    return_t ret = wish_parse_transport_ip(pretty, 0, &ip);
                                    if (ret != RET_SUCCESS) {
                                        break;
                                    }
                                    
                                    /* References to wish core, port and via relay are already initialized by wish_open_connection_dns */
                                    wish_core_t *core = resolver->wish_conn->core;
                                    uint16_t port = resolver->wish_conn->remote_port;
                                    bool via_relay = resolver->wish_conn->via_relay;
                                    
                                    wish_open_connection(core, resolver->wish_conn, &ip, port, via_relay);
                                }
                                else if (resolver->relay_client) {
                                    /* We were resolving for a relay client connection */
                                    wish_ip_addr_t ip;
                                    
                                    return_t ret = wish_parse_transport_ip(pretty, 0, &ip);
                                    if (ret != RET_SUCCESS) {
                                        break;
                                    } 
                                    port_relay_client_open(resolver->relay_client, &ip);
                                }                            
                                break; //while loop testing dns_rr_grep()
                            }
                            else {
                                WISHDEBUG(LOG_CRITICAL, "Unexpected situation when handling DNS no error result (resolver %p)", resolver);
                                abort();
                            }
                        }

                    }
                    break;
                case DNS_RC_NXDOMAIN:
                    WISHDEBUG(LOG_CRITICAL, "Could not resolve the domain name (resolver %p)", resolver);
                    port_dns_resolver_signal_error(resolver);
                    break;
                default:
                    WISHDEBUG(LOG_CRITICAL, "Unexpected DNS response code %i (resolver %p)", response_code, resolver);
                    port_dns_resolver_signal_error(resolver);
                    break;
            }
            
              //close the resolving
            free(ans);
            port_dns_resolver_delete(resolver);
        }
        else {
            WISHDEBUG(LOG_CRITICAL, "DNS resolver error %s (%i) resolver %p\n", dns_strerror(error), error, resolver);
            
            port_dns_resolver_signal_error(resolver);
            port_dns_resolver_delete(resolver);
        }
    }
   
    return 0;
}