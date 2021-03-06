/**
 * Copyright (C) 2018, ControlThings Oy Ab
 * Copyright (C) 2018, André Kaustell
 * Copyright (C) 2018, Jan Nyman
 * Copyright (C) 2018, Jepser Lökfors
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * @license Apache-2.0
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>
#include "helper.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <arpa/inet.h>
#endif
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include "utlist.h"

#include "wish_version.h"
#include "wish_connection.h"
#include "wish_event.h"
#include "wish_platform.h"

#include "wish_core.h"
#include "wish_local_discovery.h"
#include "wish_connection_mgr.h"
#include "wish_core_rpc.h"
#include "wish_identity.h"
#include "wish_time.h"
#include "wish_debug.h"

#include "fs_port.h"
#include "wish_relay_client.h"

#include "wish_port_config.h"
#include "port_select.h"
#include "port_dns.h"

#ifdef WITH_APP_TCP_SERVER
#include "app_server.h"
#endif

#ifdef _WIN32
typedef char socket_opt_t;
#else
typedef int socket_opt_t;
#endif

wish_core_t core_inst;

wish_core_t* core = &core_inst;

void error(const char *msg)
{
    perror(msg);
    abort();
}

int write_to_socket(wish_connection_t* connection, unsigned char* buffer, int len) {
    int retval = 0;
    int sockfd = *((int *) connection->send_arg);
    int n = write(sockfd,buffer,len); /* FIXME add support for partial write here. Now we fail if there is not enough space in the OS's TCP buffer. */
    
    if (n < 0) {
         printf("ERROR writing to socket: %s", strerror(errno));
         retval = 1;
    }

#ifdef WISH_CORE_DEBUG
    connection->bytes_out += len;
#endif
    
    return retval;
}

#define LOCAL_DISCOVERY_UDP_PORT 9090

void socket_set_nonblocking(int sockfd) {
#ifdef _WIN32
    unsigned long value = 1;
    if (ioctlsocket(sockfd, FIONBIO, &value) == SOCKET_ERROR) {
        perror("When setting socket to non-blocking mode");
        exit(1);
    }
#else
    int status = fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);

    if (status == -1){
        perror("When setting socket to non-blocking mode");
        abort();
    }
#endif
}



/* When the wish connection "i" is connecting and connect succeeds
 * (socket becomes writable) this function is called */
void connected_cb(wish_connection_t* connection) {
    //printf("Signaling wish session connected \n");
    wish_core_signal_tcp_event(connection->core, connection, TCP_CONNECTED);
}

void connected_cb_relay(wish_connection_t* connection) {
    //printf("Signaling relayed wish session connected \n");
    wish_core_signal_tcp_event(connection->core, connection, TCP_RELAY_SESSION_CONNECTED);
}

void connect_fail_cb(wish_connection_t* connection) {
    printf("Connect fail... \n");
    wish_core_signal_tcp_event(connection->core, connection, TCP_DISCONNECTED);
}


/**
 * Implementation of the port layer wish connection function.
 * 
 * @param core
 * @param connection
 * @param host
 * @param port
 * @param via_relay
 * @return 
 */
int wish_open_connection_dns(wish_core_t* core, wish_connection_t* connection, char* host, uint16_t port, bool via_relay) {
    connection->curr_transport_state = TRANSPORT_STATE_RESOLVING;
    
#ifdef WISH_CONNECTION_BLOCKING_DNS
    /* This is a filter. Specify that we are interested only in IPv4 addresses. */
    struct addrinfo addrinfo_filter = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *addrinfo_res = NULL;
    
    size_t port_str_max_len = 5 + 1;
    char port_str[port_str_max_len];
    snprintf(port_str, port_str_max_len, "%i", port);
    
    int addr_err = getaddrinfo(host, port_str, &addrinfo_filter, &addrinfo_res);
    
    if (addr_err == 0) {
        /* Resolving was a success. Note: we should be getting only IPv4 addresses because of the filter. */
        char* ip_str = inet_ntoa(((struct sockaddr_in*)addrinfo_res->ai_addr)->sin_addr);
        wish_ip_addr_t ip;
        wish_parse_transport_ip(ip_str, 0, &ip);
        wish_open_connection(core, connection, &ip, port, via_relay);
        freeaddrinfo(addrinfo_res);
    }
    else {
        printf("DNS resolve fail\n");
        /* Note: Don't call wish_close_connection() here, as it will do (platform-dependent) things set up by wish_open_connection(), which has not been called in this case. */
        wish_core_signal_tcp_event(core, connection, TCP_DISCONNECTED);
    }
#else

    connection->core = core;
    connection->remote_port = port;
    connection->via_relay = via_relay;
    port_dns_start_resolving_wish_conn(connection, host);  
    
#endif 
    
    return 0;
}

int wish_open_connection(wish_core_t* core, wish_connection_t* connection, wish_ip_addr_t *ip, uint16_t port, bool relaying) {
    connection->core = core;
    
    int *sockfd_ptr = malloc(sizeof(int));
    if (sockfd_ptr == NULL) {
        printf("Malloc fail");
        abort();
    }
    *(sockfd_ptr) = socket(AF_INET, SOCK_STREAM, 0);

    int sockfd = *(sockfd_ptr);
    socket_set_nonblocking(sockfd);

    wish_core_register_send(core, connection, write_to_socket, sockfd_ptr);

    if (sockfd < 0) {
        perror("socket() returns error:");
        abort();
    }

    // set ip and port to wish connection
    memcpy(connection->remote_ip_addr, ip->addr, WISH_IPV4_ADDRLEN);
    connection->remote_port = port;
    
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    
    // set ip
    char ip_str[20];
    snprintf(ip_str, 20, "%d.%d.%d.%d", ip->addr[0], ip->addr[1], ip->addr[2], ip->addr[3]);
    inet_aton(ip_str, &serv_addr.sin_addr);
    
    // set port
    serv_addr.sin_port = htons(port);
    
    int ret = connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr));
    if (ret == -1) {
        if (errno == EINPROGRESS) {
            WISHDEBUG(LOG_DEBUG, "Connect now in progress");
            connection->curr_transport_state = TRANSPORT_STATE_CONNECTING;
        }
        else {
            perror("Unhandled connect() errno");
        }
    }
    else if (ret == 0) {
        printf("Cool, connect succeeds immediately!\n");
        if (connection->via_relay) {
            connected_cb_relay(connection);
        }
        else {
            connected_cb(connection);
        }
    }
    return 0;
}

void wish_close_connection(wish_core_t* core, wish_connection_t* connection) {

    if (connection->curr_transport_state == TRANSPORT_STATE_RESOLVING) {
        /* There is a resolver created for this connection. No sockets are open yet. */
        port_dns_resolver_cancel_by_wish_connection(connection);
    }
    else {
        if (connection->send_arg != NULL) {
            int sockfd = *((int *)connection->send_arg);
            close(sockfd);
            free(connection->send_arg);
        }
    }
    
    connection->context_state = WISH_CONTEXT_CLOSING;
    
    /* Note that because we don't get a callback invocation when closing
     * succeeds, we need to excplicitly call TCP_DISCONNECTED so that
     * clean-up will happen */
    wish_core_signal_tcp_event(core, connection, TCP_DISCONNECTED);
}

/** This defines the default data directory name (under home dir) for the core*/
#define CORE_DEFAULT_DIR ".wish"

static char usage_str[] = "Wish Core " WISH_CORE_VERSION_STRING
"\n\n  Usage: %s [options]\n\
    -b don't broadcast own uid over local discovery\n\
    -l don't listen to local discovery broadcasts\n\
\n\
    -S will only open connections, but not accept incoming ones (Don't listen to wish connection port)\n\
    -s listen for incoming Wish connections\n\
    -p <port> listen for incoming connections at this TCP port\n\
    -r connect to a relay server, for accepting incoming connections via the relay.\n\
\n\
    -a <port> start \"App TCP\" interface server at port\n\
\n\
    -d Use current working directory for database files; default is to use $HOME/" CORE_DEFAULT_DIR "\n";

static void print_usage(char *executable_name) {
    printf(usage_str, executable_name);
}

/* -b Start the "server" part, and start broadcastsing local discovery
 * adverts */
bool advertize_own_uid = true;
/* -i Start core in insecure state */
bool skip_connection_acl = false;
/* -l Start to listen to adverts, and connect when advert is received */
bool listen_to_adverts = true;

/* -s Accept incoming connections  */
bool as_server = true;

/* -p The Wish TCP port to listen to (when -l or -s is given), or the port
 * to connect to when -c */
uint16_t port = 0;

/* -r <relay_host> Start a relay client session to relay host for
 * accepting connections relayed by the relay host */
struct in_addr relay_server_addr;
bool as_relay_client = true;

#ifdef WITH_APP_TCP_SERVER
/* -a <app_port> The port number of the "Application" TCP port */
bool as_app_server = true;
uint16_t app_port = 9094;
extern int app_serverfd; /* Defined in app_server.c */
extern int app_fds[];
extern enum app_state app_states[];
#endif


/** If this is set to true, the core's working dir is kept at current working directory. */
static bool override_core_wd = false;

/* Process the command line options. The function will set global
 * variables accordingly */
static void process_cmdline_opts(int argc, char** argv) {
    int opt = 0;
    while ((opt = getopt(argc, argv, "hbilc:C:R:sSp:ra:d")) != -1) {
        switch (opt) {
        case 'b':
            printf("Will not do wld broadcast!\n");
            advertize_own_uid = false;
            break;
        case 'i':
            //printf("Skip connection acl (Core is Claimable)\n");
            skip_connection_acl = true;
            break;
        case 'l':
            printf("Will not listen to wld broadcasts!\n");
            listen_to_adverts = false;
            break;
        case 'S':
            printf("Won't act as Wish server\n");
            as_server = false;
            break;
        case 's':
            as_server = true;
            break;
        case 'p':
            port = atoi(optarg);
            wish_set_host_port(core, port);
            //printf("Would use port %hu\n", port);
            break;
        case 'r':
            //printf("Acting as relay client to relay server\n");
            //inet_pton(AF_INET, optarg, &relay_server_addr);
            as_relay_client = true;
            break;
        case 'a':
#ifdef WITH_APP_TCP_SERVER
            app_port = atoi(optarg);
            //printf("Starting the app port at %hu\n", app_port);
            as_app_server = true;
#else // WITH_APP_TCP_SERVER
            printf("App tcp server not included in build!\n");
            abort();
#endif
            break;
        case 'd':
            /* Allows saving the identity database and other files to working directory instead of global place */
            override_core_wd = true;
            
            break;
        default:
            print_usage(argv[0]);
            exit(1);
            break;
        }
    }

}


/* The different sockets we are using */

/* The UDP Wish local discovery socket */
int wld_fd = 0;
struct sockaddr_in sockaddr_wld;

/* This function sets up a UDP socket for listening to UDP local
 * discovery broadcasts */
void setup_wish_local_discovery(void) {
    wld_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (wld_fd == -1) {
        error("udp socket");
    }

    /* Set socketoption REUSEADDR on the UDP local discovery socket so
     * that we can have several programs listening on the one and same
     * local discovery port 9090 */
    const socket_opt_t option = 1;
    setsockopt(wld_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    /* The following options needs to be set on
     * -OSX: so that the UDP port can be shared between processes
     * -Linux: together with do_loopback_broadcast() allows wld to work even if there are no network interfaces currently 'up'
     */
    setsockopt(wld_fd, SOL_SOCKET, SO_REUSEPORT, &option, sizeof(option));


    socket_set_nonblocking(wld_fd);

    memset((char *) &sockaddr_wld, 0, sizeof(struct sockaddr_in));
    sockaddr_wld.sin_family = AF_INET;
    sockaddr_wld.sin_port = htons(LOCAL_DISCOVERY_UDP_PORT);
    sockaddr_wld.sin_addr.s_addr = INADDR_ANY;

    if (bind(wld_fd, (struct sockaddr*) &sockaddr_wld, 
            sizeof(struct sockaddr_in))==-1) {
        error("local discovery bind()");
    }

}

/* This function reads data from the local discovery socket. This
 * function should be called when select() indicates that the local
 * discovery socket has data available */
void read_wish_local_discovery(void) {
    const int buf_len = 1024;
    uint8_t buf[buf_len];
    int blen;
    socklen_t slen = sizeof(struct sockaddr_in);

    blen = recvfrom(wld_fd, buf, sizeof(buf), 0, (struct sockaddr*) &sockaddr_wld, &slen);
    if (blen == -1) {
      error("recvfrom()");
    }

    if (blen > 0) {
        //printf("Received from %s:%hu\n\n",inet_ntoa(sockaddr_wld.sin_addr), ntohs(sockaddr_wld.sin_port));
        union ip {
           uint32_t as_long;
           uint8_t as_bytes[4];
        } ip;
        /* XXX Don't convert to host byte order here. Wish ip addresses
         * have network byte order */
        //ip.as_long = ntohl(sockaddr_wld.sin_addr.s_addr);
        ip.as_long = sockaddr_wld.sin_addr.s_addr;
        wish_ip_addr_t ip_addr;
        memcpy(&ip_addr.addr, ip.as_bytes, 4);
        //printf("UDP data from: %i, %i, %i, %i\n", ip_addr.addr[0],
        //    ip_addr.addr[1], ip_addr.addr[2], ip_addr.addr[3]);

        wish_ldiscover_feed(core, &ip_addr, 
           ntohs(sockaddr_wld.sin_port), buf, blen);
    }
}

void cleanup_local_discovery(void) {
    close(wld_fd);

}

static void do_loopback_broadcast(wish_core_t* core, uint8_t *ad_msg, size_t ad_len) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        perror("Could not create socket for broadcasting");
        abort();
    }
   
    struct sockaddr_in sockaddr_src;
    memset(&sockaddr_src, 0, sizeof (struct sockaddr_in));
    sockaddr_src.sin_family = AF_INET;
    sockaddr_src.sin_port = 0;
    if (bind(s, (struct sockaddr *)&sockaddr_src, sizeof(struct sockaddr_in)) != 0) {
        error("Send local discovery: bind()");
    }
    struct sockaddr_in si_other;
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(LOCAL_DISCOVERY_UDP_PORT);
    inet_aton("127.0.0.1", &si_other.sin_addr);
    socklen_t addrlen = sizeof(struct sockaddr_in);

    if (sendto(s, ad_msg, ad_len, 0, 
            (struct sockaddr*) &si_other, addrlen) == -1) {
        if (errno == ENETUNREACH || errno == ENETDOWN) {
            printf("wld: Network currently unreachable, or down. Retrying later. (local)\n");
        } else if (errno == EPERM) {
            printf("wld: Network returned EPERM. (local)\n");
        } else {
            error("sendto() (local)");
        }
    }

    close(s);
    
    
}

int wish_send_advertizement(wish_core_t* core, uint8_t *ad_msg, size_t ad_len) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        perror("Could not create socket for broadcasting");
        abort();
    }
    
    const socket_opt_t broadcast = 1;
    if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, 
            &broadcast, sizeof(broadcast))) {
        error("set sock opt");
    }


    struct sockaddr_in sockaddr_src;
    memset(&sockaddr_src, 0, sizeof (struct sockaddr_in));
    sockaddr_src.sin_family = AF_INET;
    sockaddr_src.sin_port = 0;
    if (bind(s, (struct sockaddr *)&sockaddr_src, sizeof(struct sockaddr_in)) != 0) {
        error("Send local discovery: bind()");
    }
    struct sockaddr_in si_other;
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(LOCAL_DISCOVERY_UDP_PORT);
    inet_aton("255.255.255.255", &si_other.sin_addr);
    socklen_t addrlen = sizeof(struct sockaddr_in);

    if (sendto(s, ad_msg, ad_len, 0, 
            (struct sockaddr*) &si_other, addrlen) == -1) {
        if (errno == ENETUNREACH || errno == ENETDOWN) {
            printf("wld: Network currently unreachable, or down. Retrying later.\n");
        } else if (errno == EPERM) {
            printf("wld: Network returned EPERM.\n");
        } else {
            error("sendto()");
        }
    }

    close(s);
    
    do_loopback_broadcast(core, ad_msg, ad_len);
    
    return 0;
}

/* The fd for the socket that will be used for accepting incoming
 * Wish connections */
int serverfd = 0;


/* This functions sets things up so that we can accept incoming Wish connections
 * (in "server mode" so to speak)
 * After this, we can start select()ing on the serverfd, and we should
 * detect readable condition immediately when a TCP client connects.
 * */
void setup_wish_server(wish_core_t* core) {
    serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd < 0) {
        perror("server socket creation");
        abort();
    }
    
    const socket_opt_t option = 1;
    setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    socket_set_nonblocking(serverfd);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof (server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(wish_get_host_port(core));
    if (bind(serverfd, (struct sockaddr *) &server_addr, 
            sizeof(server_addr)) < 0) {
        perror("ERROR on binding wish server socket");
        printf("setup_wish_server: Trying to bind port %d failed.\n", server_addr.sin_port);
        abort();
    }
    int connection_backlog = 1;
    if (listen(serverfd, connection_backlog) < 0) {
        perror("listen()");
    }
}



#ifdef _WIN32
HCRYPTPROV crypt_prov;
static int seed_random_init() {
    
    if (CryptAcquireContext(&crypt_prov, NULL, NULL, PROV_RSA_FULL, 0) != TRUE) {
        printf("Failed creating crypt cointainer (error=0x%lx), this is dangerous, bailing out.\n", GetLastError());
        exit(1);
    }
    
    return 0;
}

#if 0
static void seed_random_deinit() {
    CryptReleaseContext(crypt_prov, 0);
}
#endif

static long int random(void) {
    unsigned int randval;

    BOOL success = CryptGenRandom(crypt_prov, sizeof(randval), (BYTE *) &randval);
    if (!success) {
        printf("Failed generating random number, this is dangerous, bailing out.\n");
        exit(1);
    }
    return randval;
}

#else
static int seed_random_init() {
    unsigned int randval;
    
    FILE *f;
    f = fopen("/dev/urandom", "r");
    
    int c;
    for (c=0; c<32; c++) {
        size_t read = fread(&randval, sizeof(randval), 1, f);
        if (read != 1) {
            printf("Failed to read from /dev/urandom, this is dangerous, bailing out.\n");
            abort();
        }
        srandom(randval);
    }
    
    fclose(f);
    
    return 0;
}
#endif

/** 
 * Set the directory for data files of the wish core
 * Data files are (among others) the identity database and wish core's config file named wish.conf
 */
static void set_core_working_dir(char *path) {
    /* For now, this sets the current working dir of the process */
    
#ifdef _WIN32
    struct _stat st = {0};

    if (_stat(path, &st) == -1) {
        int mkdir_ret = mkdir(path);
        if (mkdir_ret == -1) {
            perror("When mkdir'ing core data dir");
            abort();
        }
    }
#else
    struct stat st = {0};

    if (stat(path, &st) == -1) {
        int mkdir_ret = mkdir(path, 0700);
        if (mkdir_ret == -1) {
            perror("When mkdir'ing core data dir");
            abort();
        }
    }
#endif
    int ret = chdir(path);
    if (ret == -1) {
        printf("Error while setting core data dir to %s, result: %s\n", path, strerror(errno));
        abort();
    }
    else {
        printf("Core data dir is %s\n", path);
    }
}

#define IO_BUF_LEN 1000

int main(int argc, char** argv) {
#ifdef __WIN32__
   WORD versionWanted = MAKEWORD(1, 1);
   WSADATA wsaData;
   WSAStartup(versionWanted, &wsaData);
#endif
    
    
    wish_platform_set_malloc(malloc);
    wish_platform_set_realloc(realloc);
    wish_platform_set_free(free);
    
    wish_platform_set_rng(random);
    wish_platform_set_vprintf(vprintf);
    wish_platform_set_vsprintf(vsprintf);

    wish_fs_set_open(my_fs_open);
    wish_fs_set_read(my_fs_read);
    wish_fs_set_write(my_fs_write);
    wish_fs_set_lseek(my_fs_lseek);
    wish_fs_set_close(my_fs_close);
    wish_fs_set_rename(my_fs_rename);
    wish_fs_set_remove(my_fs_remove);

    // Will provide some random, but not to be considered cryptographically secure
    seed_random_init();
    
    /* Process command line options */
    if (argc >= 2) {
        //printf("Parsing command line options.\n");
        process_cmdline_opts(argc, argv);
    } else {
        printf("Using default parameters. Start with -h for options.\n");

        advertize_own_uid = true;
        listen_to_adverts = true;
        as_server = true;
        as_relay_client = true;
        app_port = 9094;
        as_app_server = true;
        skip_connection_acl = false;
    }
    
    size_t core_wd_max_len = 1024;
    char core_wd[core_wd_max_len];
    if (!override_core_wd) {
#ifdef _WIN32
        snprintf(core_wd, core_wd_max_len, "%s\\" CORE_DEFAULT_DIR, getenv("USERPROFILE"));
#else
        snprintf(core_wd, core_wd_max_len, "%s/" CORE_DEFAULT_DIR, getenv("HOME"));
#endif
        set_core_working_dir(core_wd);
    }
    else {
        char *cwd = getcwd(core_wd, core_wd_max_len);
        if (cwd == NULL) {
            perror("When calling getcwd");
            abort();
        }
        else {
            printf("Core data dir is %s\n", core_wd);
        }
    }
        

    /* Initialize Wish core (RPC servers) */
    wish_core_init(core);

    core->config_skip_connection_acl = skip_connection_acl;
    
    wish_core_update_identities(core);
    
    if (as_server) {
        setup_wish_server(core);
    }

    if (listen_to_adverts) {
        setup_wish_local_discovery();
    }

#ifdef WITH_APP_TCP_SERVER
    if (as_app_server) {
        setup_app_server(core, app_port);
    }
#endif

    while (1) {
        port_select_reset();
        port_dns_poll_resolvers();
        
        if (as_server) {
            port_select_fd_set_readable(serverfd);
        }

        if (listen_to_adverts) {
            port_select_fd_set_readable(wld_fd);
        }

        if (as_relay_client) {
            wish_relay_client_t* relay;
            
            LL_FOREACH(core->relay_db, relay) {
                if (relay->curr_state == WISH_RELAY_CLIENT_CONNECTING) {
                    if (relay->sockfd != -1) {
                        port_select_fd_set_writable(relay->sockfd);
                    }
                }
                else if (relay->curr_state == WISH_RELAY_CLIENT_WAIT_RECONNECT) {
                    /* connect to relay server has failed or disconnected and we wait some time before retrying */
                }
                else if (relay->curr_state == WISH_RELAY_CLIENT_RESOLVING) {
                    /* Don't do anything as the resolver is resolving. relay->sockfd is not valid as it has not yet been initted! */
                }
                else if (relay->curr_state != WISH_RELAY_CLIENT_INITIAL) {
                    if (relay->sockfd != -1) {
                        port_select_fd_set_readable(relay->sockfd);
                    }
                }
            }
        }

#ifdef WITH_APP_TCP_SERVER
        if (as_app_server) {
            port_select_fd_set_readable(app_serverfd);
    
            int i;
            for (i = 0; i < NUM_APP_CONNECTIONS; i++) {
                if (app_states[i] == APP_CONNECTION_CONNECTED) {
                    port_select_fd_set_readable(app_fds[i]);
                }
            }
        }
#endif

        int i = -1;
        for (i = 0; i < WISH_PORT_CONTEXT_POOL_SZ; i++) {
            wish_connection_t* ctx = &(core->connection_pool[i]);
            if (ctx->context_state == WISH_CONTEXT_FREE) {
                continue;
            }
            else if (ctx->curr_transport_state == TRANSPORT_STATE_RESOLVING) {
                /* The transport host addr is being resolved, sockfd is not valid and indeed should not be added to any of the sets! */
                continue;
            }
            
            int sockfd = *((int *) ctx->send_arg);
            if (ctx->curr_transport_state == TRANSPORT_STATE_CONNECTING) {
                /* If the socket has currently a pending connect(), set
                 * the socket in the set of writable FDs so that we can
                 * detect when connect() is ready */
                port_select_fd_set_writable(sockfd);
            }
            else {
                port_select_fd_set_readable(sockfd);
            }
        }

        int select_ret = port_select();

        if (select_ret > 0) {

            if (port_select_fd_is_readable(wld_fd)) {
                read_wish_local_discovery();
            }

            if (as_relay_client) {
                wish_relay_client_t* relay;

                LL_FOREACH(core->relay_db, relay) {
                
                    /* Note: Before select() we added fd to be checked for writability, if the relay fd was in this state. Now we need to check writability under the same condition */
                    if (relay->curr_state ==  WISH_RELAY_CLIENT_CONNECTING && relay->sockfd != -1 && port_select_fd_is_writable(relay->sockfd)) {
                        socket_opt_t connect_error = 0;
                        socklen_t connect_error_len = sizeof(connect_error);
                        if (getsockopt(relay->sockfd, SOL_SOCKET, SO_ERROR, 
                                &connect_error, &connect_error_len) == -1) {
                            perror("Unexepected getsockopt error");
                            abort();
                        }
                        if (connect_error == 0) {
                            /* connect() succeeded, the connection is open */
                            printf("Relay client connected\n");
                            relay_ctrl_connected_cb(core, relay);
                            wish_relay_client_periodic(core, relay);
                        }
                        else {
                            /* connect fails. Note that perror() or the
                             * global errno is not valid now */
                            printf("relay control connect() failed: %s\n", strerror(connect_error));

                            close(relay->sockfd);
                            relay_ctrl_connect_fail_cb(core, relay);
                            relay->sockfd = -1;
                        }
                    }
                    else if (relay->curr_state == WISH_RELAY_CLIENT_WAIT_RECONNECT) {
                        /* connect to relay server has failed or disconnected and we wait some time before retrying  */
                    }
                    else if (relay->curr_state == WISH_RELAY_CLIENT_RESOLVING) {
                        /* Don't do anything as the resolver is resolving. relay->sockfd is not valid as it has not yet been initted! */
                    }
                    else if (relay->curr_state != WISH_RELAY_CLIENT_INITIAL && relay->sockfd != -1 && port_select_fd_is_readable(relay->sockfd)) { /* Note: Before select() we added fd to be checked for readability, if the relay fd was in some other state than its initial state. Now we need to check writability under the same condition */
                        uint8_t byte;   /* That's right, we read just one
                            byte at a time! */
                        int read_len = read(relay->sockfd, &byte, 1);
                        if (read_len > 0) {
                            wish_relay_client_feed(core, relay, &byte, 1);
                            wish_relay_client_periodic(core, relay);
                        }
                        else if (read_len == 0) {
                            printf("Relay control connection disconnected\n");
                            close(relay->sockfd);
                            relay_ctrl_disconnect_cb(core, relay);
                            relay->sockfd = -1;
                        }
                        else {
                            perror("relay control read() error (closing connection): ");
                            close(relay->sockfd);
                            relay_ctrl_disconnect_cb(core, relay);
                            relay->sockfd = -1;
                        }
                    }
                }
            }

#ifdef WITH_APP_TCP_SERVER
            if (as_app_server) {
                if (port_select_fd_is_readable(app_serverfd)) {
                    /* New connection to app server port */
                    //printf("Detected incoming App connection\n");
                    int newsockfd = accept(app_serverfd, NULL, NULL);
                    if (newsockfd < 0) {
                        perror("on accept");
                        abort();
                    }
                    socket_set_nonblocking(newsockfd);
                    int i = 0;
                    /* Find a vacant app connection "slot" */
                    for (i = 0; i < NUM_APP_CONNECTIONS; i++) {
                        if (app_states[i] == APP_CONNECTION_INITIAL) {
                            // App socketfd: newsockfd
                            app_fds[i] = newsockfd;
                            app_states[i] = APP_CONNECTION_CONNECTED;
                            break;
                        }
                    }
                    if (i >= NUM_APP_CONNECTIONS) {
                        printf("No vacant app connection found!\n");
                        close(newsockfd);
                    }
 
                }
                
                int i = 0;
                for (i = 0; i < NUM_APP_CONNECTIONS; i++) {
                    if (app_states[i] != APP_CONNECTION_CONNECTED) {
                        /* If the app state is something else than "app connected", then don't do anything. */
                        continue;
                    }
                    if (port_select_fd_is_readable(app_fds[i])) {
                        /* Existing App connection has become readable */
                        size_t buffer_len = 100;
                        uint8_t buffer[buffer_len];
                        
                        int read_len = read(app_fds[i], buffer, buffer_len);
                        
                        if (read_len > 0) {
                            /* App data can be read */
                            app_connection_feed(core, i, buffer, read_len);
                        } else if (read_len == 0) {
                            /* App has disconnected. Do clean-up */
                            //printf("App has disconnected\n");
                            app_connection_cleanup(core, i);
                            close(app_fds[i]);
                        } else {
                            //perror("app connection read()");
                            app_connection_cleanup(core, i);
                            close(app_fds[i]);
                        }             
                    }
                }
            }
#endif

            /* Check for Wish connections status changes */
            for (i = 0; i < WISH_PORT_CONTEXT_POOL_SZ; i++) {
                wish_connection_t* ctx = &(core->connection_pool[i]);
                if (ctx->context_state == WISH_CONTEXT_FREE) {
                    continue;
                }
                else if (ctx->curr_transport_state == TRANSPORT_STATE_RESOLVING) {
                    /* The transport host addr is being resolved, sockfd is not valid */
                    continue;
                }
                
                int sockfd = *((int *)ctx->send_arg);
                if (port_select_fd_is_readable(sockfd)) {
                    /* The Wish connection socket is now readable. Data
                     * can be read without blocking */
                    int rb_free = wish_core_get_rx_buffer_free(core, ctx);
                    if (rb_free == 0) {
                        /* Cannot read at this time because ring buffer
                         * is full */
                        printf("ring buffer full\n");
                        continue;
                    }
                    if (rb_free < 0) {
                        printf("Error getting ring buffer free sz\n");
                        abort();
                    }
                    const size_t read_buf_len = rb_free;
                    uint8_t buffer[read_buf_len];
                    int read_len = read(sockfd, buffer, read_buf_len);
                    if (read_len > 0) {
                        //printf("Read some data\n");
#ifdef WISH_CORE_DEBUG
                        ctx->bytes_in += read_len;
#endif
                        wish_core_feed(core, ctx, buffer, read_len);
                        wish_core_process_data(core, ctx);
                    }
                    else if (read_len == 0) {
                        //printf("Connection closed?\n");
                        close(sockfd);
                        free(ctx->send_arg);
                        wish_core_signal_tcp_event(core, ctx, TCP_DISCONNECTED);
                        continue;
                    }
                    else {
                        //read returns -1
                        close(sockfd);
                        free(ctx->send_arg);
                        wish_core_signal_tcp_event(core, ctx, TCP_DISCONNECTED);
                        continue;
                    }
                }
                if (port_select_fd_is_writable(sockfd)) {
                    /* The Wish connection socket is now writable. This
                     * means that a previous connect succeeded. (because
                     * normally we don't select for socket writability!)
                     * */
                    socket_opt_t connect_error = 0;
                    socklen_t connect_error_len = sizeof(connect_error);
                    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, 
                            &connect_error, &connect_error_len) == -1) {
                        perror("Unexepected getsockopt error");
                        abort();
                    }
                    if (connect_error == 0) {
                        /* connect() succeeded, the connection is open
                         * */
                        if (ctx->curr_transport_state 
                                == TRANSPORT_STATE_CONNECTING) {
                            if (ctx->via_relay) {
                                connected_cb_relay(ctx);
                            }
                            else {
                                connected_cb(ctx);
                            }
                        }
                        else {
                            printf("There is somekind of state inconsistency\n");
                            abort();
                        }
                    }
                    else {
                        /* connect fails. Note that perror() or the
                         * global errno is not valid now */
                        printf("wish connection connect() failed: %s\n", 
                            strerror(connect_error));
                        close(*((int*) ctx->send_arg));
                        free(ctx->send_arg);
                        connect_fail_cb(ctx);
                    }
                }

            }

            /* Check for incoming Wish connections to our server */
            if (as_server) {
                if (port_select_fd_is_readable(serverfd)) {
                    //printf("Detected incoming connection!\n");
                    int newsockfd = accept(serverfd, NULL, NULL);
                    if (newsockfd < 0) {
                        perror("on accept");
                        abort();
                    }
                    socket_set_nonblocking(newsockfd);
                    /* Start the wish core with null IDs. 
                     * The actual IDs will be established during handshake
                     * */
                    uint8_t null_id[WISH_ID_LEN] = { 0 };
                    wish_connection_t* connection = wish_connection_init(core, null_id, null_id);
                    if (connection == NULL) {
                        /* Fail... no more contexts in our pool */
                        printf("No new Wish connections can be accepted!\n");
                        close(newsockfd);
                    }
                    else {
                        int *fd_ptr = malloc(sizeof(int));
                        *fd_ptr = newsockfd;
                        /* New wish connection can be accepted */
                        wish_core_register_send(core, connection, write_to_socket, fd_ptr);
                        //WISHDEBUG(LOG_CRITICAL, "Accepted TCP connection %d", newsockfd);
                        wish_core_signal_tcp_event(core, connection, TCP_CLIENT_CONNECTED);
                    }
                }
            }


        }
        else if (select_ret == 0) {
            //printf("select() timeout\n");

        }
        else {
            /* Select error return */
            perror("Select error: ");
            abort();
        }
        
        static time_t timestamp = 0;
        if (time(NULL) > timestamp + 10) {
            timestamp = time(NULL);
            /* Perform periodic action 10s interval
             */
        }

        while (1) {
            /* FIXME this loop is bad! Think of something safer */
            /* Call wish core's connection handler task */
            struct wish_event *ev = wish_get_next_event();
            if (ev != NULL) {
                wish_message_processor_task(core, ev);
            }
            else {
                /* There is nothing more to do, exit the loop */
                break;
            }
        }

        static time_t periodic_timestamp = 0;
        if (time(NULL) > periodic_timestamp) {
            /* 1-second periodic interval */
            periodic_timestamp = time(NULL);
            wish_time_report_periodic(core);
        }
    }

    return 0;
}
 

/* This function is called when a new service is first detected */
void wish_report_new_service(wish_connection_t* connection, uint8_t* wsid, char* protocol_name_str) {
    printf("Detected new service, protocol %s", protocol_name_str);
}

/* This function is called when a Wish service goes up or down */
void wish_report_service_status_change(wish_connection_t* connection, uint8_t* wsid, bool online) {
    printf("Detected service status change");
}

