/** For testing: 
 cc -DDNS_TEST -I../../deps/dns/src -I../../src -I../../deps/wish-rpc-c99/src -I../../deps/bson -I. -I../../deps/uthash/src port_dns.c ../../deps/dns/src/dns.c
 */


#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>

#include "dns.h"
#include "port_dns.h"
#include "wish_connection.h"
#include "utlist.h"

#define EXIT_FAILURE 1

#ifdef DNS_TEST
#include <stdlib.h>
#endif

static void dns_panic(const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);

	vfprintf(stderr, fmt, ap);

	exit(EXIT_FAILURE);

} /* panic() */

#define dns_panic_(fn, ln, fmt, ...)	\
	dns_panic(fmt "%0s", (fn), (ln), __VA_ARGS__)
#define dns_panic(...)			\
	dns_panic_(__func__, __LINE__, "(%s:%d) " __VA_ARGS__, "")

static struct dns_resolv_conf *resconf(void) {
	static struct dns_resolv_conf *resconf;

	int error;

	if (resconf) {
            return resconf;
        }

	if (!(resconf = dns_resconf_open(&error))) {
		dns_panic("dns_resconf_open: %s", dns_strerror(error));
        }

        char *resconf_path = "/etc/resolv.conf";
        error = dns_resconf_loadpath(resconf, resconf_path);

        if (error) {
            dns_panic("%s: %s", resconf_path, dns_strerror(error));
	}

        char *nssconf_path = "/etc/nssconf.conf";
	error = dns_nssconf_loadpath(resconf, nssconf_path);
			
        if (error != ENOENT) {
            dns_panic("%s: %s", nssconf_path, dns_strerror(error));
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
            dns_panic("%s: %s", "/etc/hosts", dns_strerror(error));
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

struct port_dns_resolver* resolver_list = NULL;

int port_dns_start_resolving_wish_conn(wish_connection_t *conn, char *qname) {
    const _Bool recurse = false;
    
    struct dns_hints *(*hints)() = (recurse)? &dns_hints_root : &dns_hints_local;
    
    enum dns_type qtype = DNS_T_A;
    int error = 0;

    resconf()->options.recurse = recurse;

    struct dns_resolver *R = NULL;
    
    if (!(R = dns_res_open(resconf(), hosts(), dns_hints_mortal(hints(resconf(), &error)), cache(), dns_opts(), &error))) {
            dns_panic("%s: %s", qname, dns_strerror(error));
    }

    if ((error = dns_res_submit(R, qname, qtype, DNS_C_IN))) {
            dns_panic("%s: %s", qname, dns_strerror(error));
    }
    
    struct port_dns_resolver *port_resolver = malloc(sizeof(struct port_dns_resolver));
    memset(port_resolver, 0, sizeof(struct port_dns_resolver));
    
    port_resolver->R = R;
    port_resolver->wish_conn = conn;
    
    /* Add the newly allocated resolver to our list of resolvers */
    LL_APPEND(resolver_list, port_resolver);
    
    return 0;
}

int port_dns_start_resolving_relay_client(wish_relay_client_t *rc, char *qname) {
    const _Bool recurse = false;
    
    struct dns_hints *(*hints)() = (recurse)? &dns_hints_root : &dns_hints_local;
    
    enum dns_type qtype = DNS_T_A;
    int error = 0;

    resconf()->options.recurse = recurse;

    struct dns_resolver *R = NULL;
    
    if (!(R = dns_res_open(resconf(), hosts(), dns_hints_mortal(hints(resconf(), &error)), cache(), dns_opts(), &error))) {
            dns_panic("%s: %s", qname, dns_strerror(error));
    }

    if ((error = dns_res_submit(R, qname, qtype, DNS_C_IN))) {
            dns_panic("%s: %s", qname, dns_strerror(error));
    }
    
    struct port_dns_resolver *port_resolver = malloc(sizeof(struct port_dns_resolver));
    memset(port_resolver, 0, sizeof(struct port_dns_resolver));
    
    port_resolver->R = R;
    port_resolver->relay_client = rc;
    
    /* Add the newly allocated resolver to our list of resolvers */
    LL_APPEND(resolver_list, port_resolver);
    
    return 0;
}

int dns_poll_resolvers(void) {
    
    /* For each resolver in list of resolvers ... */
    struct port_dns_resolver *resolver = NULL, *tmp = NULL;
    LL_FOREACH_SAFE(resolver_list, resolver, tmp) {
        int error = dns_res_check(resolver->R);
    
        if (dns_res_elapsed(resolver->R) > 30) {
            dns_panic("query timed-out");
            
            //Close the resolving, and signal wish core (connection or relay client)
        }

        if (error == EAGAIN) {
            /* Continue polling */
#ifdef DNS_TEST
            dns_res_poll(resolver->R, 1);
#endif
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
                                printf("Resolved to: %s\n", pretty);
                                
#ifndef DNS_TEST
                                //Continue opening the connection with new info (connection or relay client)
                                assert(resolver->wish_conn != NULL ^ resolver->relay_client != NULL);
                                if (resolver->wish_conn) {
                                    
                                }
                                else if (resolver->relay_client) {
                                    
                                }
#endif                                
                                break; //while loop testing dns_rr_grep()
                            }
                            else {
                                dns_panic("Unexpected!");
                            }
                        }

                    }
                    break;
                case DNS_RC_NXDOMAIN:
                    printf("Could not resolve the domain name\n");
#ifndef DNS_TEST
                    //abort connection
#endif
                    break;
                default:
                    printf("Unexpected DNS response code %i\n", response_code);
#ifndef DNS_TEST
                    //abort connection
#endif
                    break;
            }
            
            
            free(ans);

            dns_res_close(resolver->R);
            LL_DELETE(resolver_list, resolver);
            free(resolver);
            
            
            //close the resolving
#ifdef DNS_TEST
            exit(0);
#endif
        }
        else {
            dns_panic("dns_res_check: %s (%d)", dns_strerror(error), error);
        }
    }
   
    return 0;
}
    
#ifdef DNS_TEST

int main(void) {
    port_dns_start_resolving_wish_conn(NULL, "nowww.controlthings.fi");
    while (1) {
        dns_poll_resolvers();
    }
}

#endif