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
#include <string.h>

#include "wish_connection.h"
#include "wish_debug.h"
#include "wish_connection_mgr.h"

#include "wish_relay_client.h"
#include "wish_time.h"

#include "utlist.h"

#include "wish_core.h"
#include "wish_ip_addr.h"

void relay_ctrl_connected_cb(wish_core_t* core, wish_relay_client_t *relay) {
    WISHDEBUG(LOG_CRITICAL, "Relay control connection established");
    relay->curr_state = WISH_RELAY_CLIENT_OPEN;
    relay->last_input_timestamp = wish_time_get_relative(core);
}

void relay_ctrl_connect_fail_cb(wish_core_t* core, wish_relay_client_t *relay) {
    WISHDEBUG(LOG_CRITICAL, "Relay control connection fails\n");
    relay->curr_state = WISH_RELAY_CLIENT_WAIT_RECONNECT;
    
    // Used for reconnect timeout
    relay->last_input_timestamp = wish_time_get_relative(core);
}

void relay_ctrl_disconnect_cb(wish_core_t* core, wish_relay_client_t *relay) {
    //WISHDEBUG(LOG_CRITICAL, "Relay control connection disconnected");
    relay->curr_state = WISH_RELAY_CLIENT_WAIT_RECONNECT;

    // Used for reconnect timeout
    relay->last_input_timestamp = wish_time_get_relative(core);
}

static void wish_core_relay_periodic(wish_core_t* core, void* ctx) {
    wish_relay_client_t* relay;

    LL_FOREACH(core->relay_db, relay) {
        switch(relay->curr_state) {
            case WISH_RELAY_CLIENT_CONNECTING:
                if (wish_time_get_relative(core) > (relay->last_input_timestamp + RELAY_CLIENT_CONNECT_TIMEOUT)) {
                    wish_relay_client_close(core, relay);
                }
                break;
            case WISH_RELAY_CLIENT_INITIAL:
                if (core->loaded_num_ids > 0) {
                    // Assume first identity in db is the one we want
                    // FIXME This does not work with multiple identities!
                    wish_relay_client_open(core, relay, core->uid_list[0].uid);
                }
                break;
            case WISH_RELAY_CLIENT_WAIT_RECONNECT:
                if ( wish_time_get_relative(core) > relay->last_input_timestamp + RELAY_CLIENT_RECONNECT_TIMEOUT) {
                    relay->curr_state = WISH_RELAY_CLIENT_INITIAL;
                }
                break;
            case WISH_RELAY_CLIENT_WAIT:
                if (wish_time_get_relative(core) > (relay->last_input_timestamp + RELAY_SERVER_TIMEOUT)) {
                    WISHDEBUG(LOG_CRITICAL, "Relay control connection time-out");
                    wish_relay_client_close(core, relay);
                } else {
                    // Normal operation, waiting for input (keep alive or new connection event etc.)
                    wish_relay_client_periodic(core, relay);
                }
                break;
            case WISH_RELAY_CLIENT_RESOLVING:
                //FIXME handle DNS resolve timeout it here vs. handle in port code? Now we handle it in port code...
                break;
            default:
                break;
        }
    }                
}

static int wish_core_get_num_relays(wish_core_t *core) {
    wish_relay_client_t* elt;
    int num_relays = 0;
    LL_COUNT(core->relay_db, elt, num_relays);
    return num_relays;
}

void wish_core_relay_client_init(wish_core_t* core) {
    if (wish_core_get_num_relays(core) == 0) {
        /* If there are no relay servers configured, add our 'preferred' relay server */
        wish_relay_client_add(core, RELAY_SERVER_HOST);
    }
    
    wish_core_time_set_interval(core, wish_core_relay_periodic, NULL, 1);
}

void wish_relay_client_add(wish_core_t* core, const char* host) {
    int size = sizeof(wish_relay_client_t);
    wish_relay_client_t* relay = wish_platform_malloc(size);
    memset(relay, 0, size);

    size_t host_len = strnlen(host, RELAY_SERVER_HOST_MAX_LEN);
    if (wish_parse_transport_host_port(host, host_len, relay->host, &relay->port) != RET_SUCCESS) {
        WISHDEBUG(LOG_CRITICAL, "wish_realy_client_add: Cannot parse transport host port");
        wish_platform_free(relay);
        return;
    }


    wish_relay_client_t* elt;
    
    bool found = false;
    LL_FOREACH(core->relay_db, elt) {
        if ( strncmp(elt->host, relay->host, RELAY_SERVER_HOST_MAX_LEN) == 0 && elt->port == relay->port ) {
            // already in list, bailing
            found = true;
            break;
        }
    }
    
    if (!found) {
        LL_APPEND(core->relay_db, relay);
    } else {
        wish_platform_free(relay);
    }
}

/* This function should be invoked regularly to process data received
 * from relay server and take actions accordingly */
void wish_relay_client_periodic(wish_core_t* core, wish_relay_client_t *relay) {
again:
    switch (relay->curr_state) {
    case WISH_RELAY_CLIENT_RESOLVING:
    case WISH_RELAY_CLIENT_CONNECTING:
        
        break;
    case WISH_RELAY_CLIENT_OPEN:
        /* Establishing a Relay control connection:
         * After opening the TCP socket, the relay client must
         * send a Wish preable specifying connection type 6, and the UID
         * we request relaying for, so 3+32 bytes. */
        {
            const size_t handshake_len = 3+32;  /* preamble + uid */
            uint8_t handshake_data[handshake_len];
            handshake_data[0] = 'W';
            handshake_data[1] = '.';
            handshake_data[2] = (WISH_WIRE_VERSION << 4) | 
                WISH_WIRE_TYPE_RELAY_CONTROL;   /* Type: 6 */
            memcpy(handshake_data + 3, relay->uid, WISH_ID_LEN);
            relay->send(relay->sockfd, handshake_data, handshake_len);
            /* Advance state */
            relay->curr_state = WISH_RELAY_CLIENT_READ_SESSION_ID;
        }
        break;
    case WISH_RELAY_CLIENT_READ_SESSION_ID:
        /* If there are 10 bytes to be read from server... */
        if (ring_buffer_length(&(relay->rx_ringbuf)) >= RELAY_SESSION_ID_LEN) {
            /* Read the relay session ID, 10 bytes, storing it in the
             * relay context */
            ring_buffer_read(&(relay->rx_ringbuf), relay->session_id, 
                RELAY_SESSION_ID_LEN);
            /* Advance state */
            relay->curr_state = WISH_RELAY_CLIENT_WAIT;
            
            //WISHDEBUG(LOG_CRITICAL, "Relay provided by: %i.%i.%i.%i:%d", relay->ip.addr[0], relay->ip.addr[1], relay->ip.addr[2], relay->ip.addr[3], relay->port);
            
            /* This a convenient place to make a first connection check, because we know at this point that we have a working Internet connection */
            wish_connections_check(core); 
        }
        break;
    case WISH_RELAY_CLIENT_WAIT:
        /* In this state the relay client expects the server to send
         * regular "keep-alive" messages (a '.' character every 10 secs)
         */
        /* FIXME How do we handle connection close? */
        if (ring_buffer_length(&(relay->rx_ringbuf)) >= 1) {
            uint8_t byte = 0;
            ring_buffer_read(&(relay->rx_ringbuf), &byte, 1);
            switch (byte) {
            case '.':
                /* Keepalive received - just ignore it */
                WISHDEBUG(LOG_DEBUG, "Relay: received keep-alive");
                break;
            case ':': {
                /* We have a connection attempt to the relayed uid -
                 * Start accepting it! */
                //WISHDEBUG(LOG_CRITICAL, "Relay: connection attempt!");

                /* Action plan: Open new Wish connection
                 * then send the session ID 
                 * Then proceed as if we were accepting a normal
                 * incoming Wish connection (in "server role", so to speak)
                 */

                /* Initialise connection with null IDs. 
                 * The actual IDs will be established during handshake
                 * */
                uint8_t null_id[WISH_ID_LEN] = { 0 };
                wish_connection_t* connection = wish_connection_init(core, null_id, null_id);
                /* Register the relay context to the newly created wish
                 * context, this is because we need to send over the
                 * relay session id */
                if (connection == NULL) {
                    WISHDEBUG(LOG_CRITICAL, "Cannot accept new connections at this time. Please try again later!");
                    break;
                }
                connection->relay = relay;
                connection->via_relay = true;

                /* Now determine if we have an IP address, or do we need to resolve */
                wish_ip_addr_t ip;
                if (wish_parse_transport_ip(relay->host, 0, &ip) == RET_SUCCESS) {
                    /* We have an IP addr as relay hostname */
                    wish_open_connection(core, connection, &ip, relay->port, true);
                }
                else {
                    wish_open_connection_dns(core, connection, relay->host, relay->port, true);
                }
                goto again;
                break;
            }
            default:
                WISHDEBUG(LOG_CRITICAL, "Relay error: Unexepected data");
                break;
            }
        }

        break;
    case WISH_RELAY_CLIENT_CLOSING:
        WISHDEBUG(LOG_CRITICAL, "Waiting for relay client control connection to close properly");
        break;
    case WISH_RELAY_CLIENT_WAIT_RECONNECT:
        
        break;
    case WISH_RELAY_CLIENT_INITIAL:
        WISHDEBUG(LOG_CRITICAL, "Illegal wish relay state");
        break;
    }

}

void wish_relay_client_feed(wish_core_t* core, wish_relay_client_t *relay, uint8_t *data, size_t data_len) {
    ring_buffer_write(&(relay->rx_ringbuf), data, data_len);
    relay->last_input_timestamp = wish_time_get_relative(core);
}


int wish_relay_get_preferred_server_url(char *url_str, int url_str_max_len) {
    wish_platform_sprintf(url_str, "wish://" RELAY_SERVER_HOST);
    return 0;
}

int wish_relay_encode_as_url(char *url_str, char *host, int port) {
    wish_platform_sprintf(url_str, "wish://%s:%d", host, port);
    return 0;
}
