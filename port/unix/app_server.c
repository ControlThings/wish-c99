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
/* This is the implementation for the Mist C99 simple "App" TCP
 * interface (ie. the one without any kind of encryption in app
 * connections)
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>

#ifdef _WIN32
#include <inttypes.h>
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <arpa/inet.h>
#endif

#include <time.h>
#include <fcntl.h>
#include <errno.h>

#include "wish_connection.h"
#include "wish_event.h"
#include "wish_platform.h"
#include "wish_debug.h"

#include "bson.h"
#include "bson_visit.h"

#include "wish_local_discovery.h"
#include "wish_connection_mgr.h"
#include "wish_core_rpc.h"
#include "wish_identity.h"
#include "wish_time.h"
#include "core_service_ipc.h"


#include "fs_port.h"
#include "wish_relay_client.h"


#include "app_server.h"


/* Prototypes */
void socket_set_nonblocking(int sockfd);

int app_serverfd = 0;

/* This array holds the fds for app connections 
 * FIXME these separate arrays must be put into a shared structure representing an 'app connection' 
 */
int app_fds[NUM_APP_CONNECTIONS];
enum app_state app_states[NUM_APP_CONNECTIONS];
ring_buffer_t app_rx_ring_bufs[NUM_APP_CONNECTIONS];

uint16_t app_transport_expect_bytes[NUM_APP_CONNECTIONS];
enum app_transport_state app_transport_states[NUM_APP_CONNECTIONS];
bool app_transport_cont_bytes_flag[NUM_APP_CONNECTIONS];
uint8_t* app_transport_cont_bytes_buffer[NUM_APP_CONNECTIONS];
size_t app_transport_cont_bytes_total[NUM_APP_CONNECTIONS];
size_t app_transport_cont_bytes_so_far[NUM_APP_CONNECTIONS];

struct app_entry {
    uint8_t wsid[WISH_WSID_LEN];
};
static struct app_entry apps[NUM_APP_CONNECTIONS];

bool app_login_complete[NUM_APP_CONNECTIONS];


/** This function sets up the app server listening socket so that App
 * clients can be accepted when select detects incoming connection
 * (indicated by fd turning to readable)
 *
 * @param app_port the TCP port where the app server should bind to
 */
void setup_app_server(wish_core_t* core, uint16_t app_port) {
    //printf("App server starting\n");
    app_serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (app_serverfd < 0) {
        perror("App server socket creation");
        abort();
    }
#ifdef _WIN32
    const char option = 1;
    setsockopt(app_serverfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
#else
    int option = 1;
    setsockopt(app_serverfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
#endif
    socket_set_nonblocking(app_serverfd);
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof (server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   //Replace with INADDR_ANY if you wish to allow remote apps, but beware!
    server_addr.sin_port = htons(app_port);
    if (bind(app_serverfd, (struct sockaddr *) &server_addr, 
            sizeof(server_addr)) < 0) {
        perror("ERROR on binding");
        abort();
    }
    int connection_backlog = NUM_APP_CONNECTIONS;
    if (listen(app_serverfd, connection_backlog) < 0) {
        perror("listen()");
    }

    /* Setup app connection ring buffers */
    int i = 0;
    for (i = 0; i < NUM_APP_CONNECTIONS; i++) {
        uint8_t *backing = (uint8_t *) malloc(APP_RX_RB_SZ);
        if (backing == NULL) {
            printf("Could not allocate app connection rb backing\n");
            abort();
        }
        ring_buffer_init(&app_rx_ring_bufs[i], backing, APP_RX_RB_SZ);
        app_transport_states[i] = APP_TRANSPORT_INITIAL;
    }
}

bool is_app_via_tcp(wish_core_t* core, const uint8_t wsid[WISH_WSID_LEN]) {
    bool retval = false;
    int i = 0;
    for (i = 0; i < NUM_APP_CONNECTIONS; i++) {
        if (memcmp(apps[i].wsid, wsid, WISH_WSID_LEN) == 0) {
            /* Found the app! */
            retval = true;
            break;
        }
    }

    return retval;
}

void send_core_to_app_via_tcp(wish_core_t* core, const uint8_t wsid[WISH_ID_LEN], const uint8_t *data, size_t len) {
    /* Find app index */
    int i = 0;
    for (i = 0; i < NUM_APP_CONNECTIONS; i++) {
        if (memcmp(apps[i].wsid, wsid, WISH_ID_LEN) == 0) {
            /* Found our app connection */
            size_t sent_len = 0;

            do {
                const size_t frame_max_len = 65535-2;
                uint16_t frame_len = (len -sent_len) > frame_max_len ? frame_max_len: (len - sent_len); // ((len & 0xff) << 8) | (len >> 8);

                char hdr[2];
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
                hdr[0] = frame_len & 0xff;
                hdr[1] = frame_len >> 8;
    #else 
                hdr[0] = frame_len >> 8;
                hdr[1] = frame_len & 0xff;
    #endif   
                
                //char* p = (char*)&frame_len;
                //printf("Found app connection, going to send %lu bytes, setting frame len to 0x%02x%02x\n", len, p[0] & 0xff, p[1] & 0xff);

                char buf_c[65535];
                char* buf = buf_c;
                
                memcpy(buf, hdr, 2);
                memcpy(buf+2, data+sent_len, frame_len);
                
    #ifdef __APPLE__
                ssize_t write_ret = send(app_fds[i], buf, 2+frame_len, SO_NOSIGPIPE);
    #else
    #ifdef _WIN32
                ssize_t write_ret = send(app_fds[i], buf, 2+frame_len, 0);
    #else
                ssize_t write_ret = send(app_fds[i], buf, 2+frame_len, MSG_NOSIGNAL);
    #endif
    #endif
                
                if (write_ret != 2+frame_len) {
                    //printf("App connection: Write error! (c) Wanted %i got %zd\n", 2, write_ret);
                    //close(app_fds[i]);
                    return;
                }
                sent_len += frame_len;
                assert(sent_len <= len);
            } while (sent_len < len);
            return;
        }
    }
}


void app_connection_feed(wish_core_t* core, int i, uint8_t *buffer, size_t buffer_len) {
    //printf("Feeding %i bytes from app %i\n", (int) buffer_len, i);
    ring_buffer_write(&app_rx_ring_bufs[i], buffer, buffer_len);

again:
    switch (app_transport_states[i]) {
    case APP_TRANSPORT_INITIAL:
        /* We expect to get the preabmle bytes first */
        if (ring_buffer_length(&app_rx_ring_bufs[i]) < 3) {
            /* Not enough data to read yet */
            break;
        }
        else {
            /* There enough data to read so we can see if we got the
             * preamble! */
            uint8_t preamble[3];
            ring_buffer_read(&app_rx_ring_bufs[i], preamble, 3);
            if (preamble[0] == 'W' 
                    && preamble[1] == '.' 
                    && preamble[2] == 0x18) {
                printf("Error: App server secure handshake not implemented.\n");
                app_transport_states[i] = APP_TRANSPORT_CLOSING;
                break;
            }
            else if (preamble[0] == 'W' 
                    && preamble[1] == '.' 
                    && preamble[2] == 0x19) {
                //printf("App server handshake OK\n");
                /* Handshake OK, FALLTHROUGH to next case */
                app_transport_states[i] = APP_TRANSPORT_WAIT_FRAME_LEN;
            }
            else {
                printf("App server handshake error, version %d, type %d\n", preamble[2]>>4, preamble[2] & 0x0F);
                break;
            }
        }
        /* FALLTHROUGH */
    case APP_TRANSPORT_WAIT_FRAME_LEN:
        if (ring_buffer_length(&app_rx_ring_bufs[i]) >= 2) {
            uint8_t len_bytes[2];
            ring_buffer_read(&app_rx_ring_bufs[i], len_bytes, 2);
            uint16_t expect_len = len_bytes[0] << 8 | len_bytes[1];
            app_transport_expect_bytes[i] = expect_len;
            
            // skip frame payload if len is 0
            if (expect_len == 0) { goto again; }
            
            app_transport_states[i] = APP_TRANSPORT_WAIT_PAYLOAD;
            
            if (expect_len>APP_RX_RB_SZ) {
                printf("app_server.c: Buffer too small! %i (expecting: %i)\n", APP_RX_RB_SZ, expect_len);
            }
            
            if (ring_buffer_length(&app_rx_ring_bufs[i]) >= 
                    app_transport_expect_bytes[i]) {
                goto again;
            }
        }
        break;
    case APP_TRANSPORT_WAIT_PAYLOAD: {
        uint16_t expect_len = app_transport_expect_bytes[i];
        if (ring_buffer_length(&app_rx_ring_bufs[i]) >= expect_len) {
            uint8_t payload[expect_len];
            uint16_t rb_read = ring_buffer_read(&app_rx_ring_bufs[i], payload, expect_len);
            if (rb_read != expect_len) {
                WISHDEBUG(LOG_CRITICAL, "rb_read mismatch, %i while expecting %i", rb_read, expect_len);
                abort();
            }
            app_transport_states[i] = APP_TRANSPORT_WAIT_FRAME_LEN;
            
            //printf("Received whole frame! len = %i\n", expect_len);
            
            if (app_login_complete[i] == false) {
                /* Snatch WSID */

                bson_iterator it;
                
                if (bson_find_from_buffer(&it, payload, "wsid") == BSON_BINDATA 
                        && bson_iterator_bin_len(&it) == WISH_WSID_LEN) 
                {
                    memcpy(apps[i].wsid, bson_iterator_bin_data(&it), WISH_WSID_LEN);
                    app_login_complete[i] = true;
                } else {
                    bson_visit("Bad login message!", payload);
                }
            }

            bson bs;
            if (app_transport_cont_bytes_flag[i]) {
                /* Handle that a previous frame was the start of a large BSON structure, which did not fit one frame. */
                memcpy(app_transport_cont_bytes_buffer[i] + app_transport_cont_bytes_so_far[i], payload, expect_len);
                app_transport_cont_bytes_so_far[i] += expect_len;

                if (app_transport_cont_bytes_so_far[i] < app_transport_cont_bytes_total[i]) {
                    /* BSON still not received completely, still more frames to come! */
                    goto again;
                }

                /* All bytes comprising the BSON structure have now arrived */
                
                assert(app_transport_cont_bytes_so_far[i] == app_transport_cont_bytes_total[i]);
                bson_init_with_data(&bs, app_transport_cont_bytes_buffer[i]);
                receive_app_to_core(core, apps[i].wsid, app_transport_cont_bytes_buffer[i], bson_size(&bs) );

                /* reset the state */
                app_transport_cont_bytes_flag[i] = false;
                free(app_transport_cont_bytes_buffer[i]);
                app_transport_cont_bytes_so_far[i] = 0;
                app_transport_cont_bytes_total[i] = 0;
            }
            else {
            
                bson_init_with_data(&bs, payload);
            
                if ( bson_size(&bs) > expect_len) {
                    /* The BSON data did not fit in one app-to-core frame, wait for more, which will be then assembled into one  */
                    app_transport_cont_bytes_so_far[i] = expect_len;
                    app_transport_cont_bytes_total[i] =  bson_size(&bs);
                    app_transport_cont_bytes_buffer[i] = malloc(bson_size(&bs));
                    app_transport_cont_bytes_flag[i] = true;
                    memcpy(app_transport_cont_bytes_buffer[i], payload, expect_len);
                    goto again;
                }
                else if ( bson_size(&bs) != expect_len ) {
                    WISHDEBUG(LOG_CRITICAL, "Payload size mismatch, %i while expecting %i", bson_size(&bs), expect_len);
                    app_transport_states[i] = APP_TRANSPORT_CLOSING;
                    break;
                }
                /* A BSON structure fitting in one app-to-core transport frame was received */
                receive_app_to_core(core, apps[i].wsid, payload, expect_len );
            }

            if (ring_buffer_length(&app_rx_ring_bufs[i]) >= 2) {
                goto again;
            }
        }

        break;
    }
    case APP_TRANSPORT_CLOSING:
        WISHDEBUG(LOG_CRITICAL, "This transport is in CLOSING state, server is disregarding.");
        /* FIXME unhandled! */
        break;
    }
}


void app_connection_cleanup(wish_core_t* core, int i) {
    if (app_states[i] != APP_CONNECTION_CONNECTED) {
        WISHDEBUG(LOG_CRITICAL, "Illegal app state when app_connection_cleanup was called!");
        return;
    }
    
    /* We should now notify the Wish core that the service has gone way. The core will then send 'peers' updates ("offline-messages") to other cores which are subscribed to 'peers' */
    wish_service_register_remove(core, apps[i].wsid);
    
    /* Low-level clean-up: */
    app_states[i] = APP_CONNECTION_INITIAL;
    app_transport_states[i] = APP_TRANSPORT_INITIAL;
    app_login_complete[i] = false;
    memset(apps[i].wsid, 0, WISH_ID_LEN);

    /* Empty the ring buffer so that no trashes are left */
    uint16_t len = ring_buffer_length(&app_rx_ring_bufs[i]);
    ring_buffer_skip(&app_rx_ring_bufs[i], len);
}
