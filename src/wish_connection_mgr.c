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
#include <stdint.h>

#include "wish_port_config.h"
#include "wish_core.h"
#include "wish_utils.h"
#include "wish_identity.h"
#include "wish_debug.h"
#include "wish_connection.h"
#include "bson.h"
#include "bson_visit.h"
#include "wish_connection_mgr.h"
#include "string.h"

void wish_connections_init(wish_core_t* core) {
    core->connection_pool = wish_platform_malloc(sizeof(wish_connection_t)*WISH_CONTEXT_POOL_SZ);
    memset(core->connection_pool, 0, sizeof(wish_connection_t)*WISH_CONTEXT_POOL_SZ);
    core->next_conn_id = 1;
    
    wish_core_time_set_interval(core, &check_connection_liveliness, NULL, 1);
}

void wish_connections_check(wish_core_t* core) {
    int num_uids_in_db = wish_get_num_uid_entries();
    wish_uid_list_elem_t uid_list[num_uids_in_db];
    int num_uids = wish_load_uid_list(uid_list, num_uids_in_db);

    int i = 0;

    int j;
    for (j = 0; j < num_uids; j++) {
        if (i == j) { continue; }
        if( wish_core_is_connected_luid_ruid(core, uid_list[0].uid, uid_list[j].uid) ) { continue; }
        
        wish_identity_t id;
        if (wish_identity_load(uid_list[j].uid, &id) != RET_SUCCESS) {
            WISHDEBUG(LOG_CRITICAL, "Failed loading identity");
            wish_identity_destroy(&id);
            return;
        }
        
        /* Check if we should connect, meta: { connect: false } */
        
        if (wish_identity_get_meta_connect(&id) == false) {
            WISHDEBUG(LOG_CRITICAL, "check connections: will not connect, %s flagged as 'do not connect'", id.alias);
            wish_identity_destroy(&id);
            continue;
        }
        
        /* Check if we should connect, permissions: { banned: true } */
        if (wish_identity_is_banned(&id) == true) {
            WISHDEBUG(LOG_CRITICAL, "check connections, will not connect, %s is flagged as 'banned'", id.alias);
            wish_identity_destroy(&id);
            continue;
        }
        
        
#if 0  
        /* Check if we should connect, meta: { outgoingFriendRequest: true } */
        if (wish_identity_get_meta_unconfirmed_friend_request(&id) == true) {
            WISHDEBUG(LOG_CRITICAL, "check connections, will not connect, %s is flagged as 'outgoingFriendRequest'", id.alias);
            wish_identity_destroy(&id);
            continue;
        }
#endif        
                
        for (int cnt = 0; cnt < WISH_MAX_TRANSPORTS; cnt++) {
            int url_len = strnlen(id.transports[cnt], WISH_MAX_TRANSPORT_LEN);
            if (url_len > 0) {
                char* url = id.transports[cnt];
                //WISHDEBUG(LOG_CRITICAL, "  Should connect %02x %02x > %02x %02x to %s", uid_list[0].uid[0], uid_list[0].uid[1], uid_list[j].uid[0], uid_list[j].uid[1], url);

                wish_connections_connect_transport(core, uid_list[0].uid, uid_list[j].uid, url);
            }
        }
        wish_identity_destroy(&id);
    }
}

/* This function will check the connections and send a 'ping' if they
 * have not received anything lately */
void check_connection_liveliness(wish_core_t* core, void* ctx) {
    //WISHDEBUG(LOG_CRITICAL, "check_connection_liveliness");
    int i = 0;
    for (i = 0; i < WISH_CONTEXT_POOL_SZ; i++) {
        wish_connection_t* connection = &(core->connection_pool[i]);
        switch (connection->context_state) {
        case WISH_CONTEXT_CONNECTED:
            /* We have found a connected context we must examine */
            if ((core->core_time > (connection->latest_input_timestamp + PING_INTERVAL))
                && (connection->ping_sent_timestamp <= connection->latest_input_timestamp)) 
            {
                WISHDEBUG(LOG_DEBUG, "Pinging connection %d", i);
 
                /* Enqueue a ping message */
                const size_t ping_buffer_sz = 128;
                uint8_t ping_buffer[ping_buffer_sz];
                bson ping;
                bson_init_buffer(&ping, ping_buffer, ping_buffer_sz);
                
                bson_append_bool(&ping, "ping", true);
                
                bson_finish(&ping);
                
                wish_core_send_message(core, connection, bson_data(&ping), bson_size(&ping));
                connection->ping_sent_timestamp = core->core_time;
            }

            if (core->core_time > (connection->latest_input_timestamp + PING_TIMEOUT) &&
                (connection->ping_sent_timestamp > connection->latest_input_timestamp)) {
                WISHDEBUG(LOG_CRITICAL, "Connection ping: Killing connection because of inactivity");
                wish_close_connection(core, connection);
            }
            break;
        case WISH_CONTEXT_IN_MAKING: {
            if (core->core_time > (connection->latest_input_timestamp + CONNECTION_SETUP_TIMEOUT)) {
#ifndef WISH_CORE_DEBUG
                WISHDEBUG(LOG_CRITICAL, "Ping timeout. Closing connection. (luid: %02x %02x, ruid: %02x %02x)", 
                        connection->luid[0], connection->luid[1], connection->ruid[0], connection->ruid[1]);
#else
                WISHDEBUG(LOG_CRITICAL, "Ping timeout. Closing connection. (luid: %02x %02x, ruid: %02x %02x), tp: %d, ps: %d, b_in: %i, b_out: %i, rb: %d, expect: %d, time: %d, core-time: %d (%s, %s)", 
                        connection->luid[0], connection->luid[1], connection->ruid[0], connection->ruid[1], 
                        connection->curr_transport_state, connection->curr_protocol_state, connection->bytes_in, connection->bytes_out, ring_buffer_length(&connection->rx_ringbuf), connection->expect_bytes, 
                        connection->latest_input_timestamp, wish_time_get_relative(core), connection->via_relay ? "relayed" : "direct",
                        connection->friend_req_connection ? "friendReq" : "normal");
#endif
                wish_close_connection(core, connection);
            }
            break;
        }
        case WISH_CONTEXT_CLOSING:
            WISHDEBUG(LOG_CRITICAL, "Connection ping: Found context in closing state! Forcibly closing it.");
            wish_close_connection(core, connection);
            break;
        case WISH_CONTEXT_FREE:
            /* Obviously we don't ping unused contexts! */
            break;
        }
    }
}

return_t wish_connections_connect_transport(wish_core_t* core, uint8_t *luid, uint8_t *ruid, char* transport) {
    
    wish_identity_t lu;
    wish_identity_t ru;
    
    if ( RET_SUCCESS != wish_identity_load(luid, &lu) 
            || RET_SUCCESS != wish_identity_load(ruid, &ru) )
    {
        wish_identity_destroy(&lu);
        wish_identity_destroy(&ru);
        return RET_FAIL;
    }
    
    wish_identity_destroy(&lu);
    wish_identity_destroy(&ru);
    
    wish_connection_t* connection = wish_connection_init(core, luid, ruid);
    
    if (connection != NULL) {
        //WISHDEBUG(LOG_CRITICAL, "Connection attempt: %s > %s (%u.%u.%u.%u:%hu)", lu.alias, ru.alias, ip->addr[0], ip->addr[1], ip->addr[2], ip->addr[3], port);
        
        wish_ip_addr_t ip;
        uint16_t port;
        size_t transport_len = strnlen(transport, WISH_MAX_TRANSPORT_LEN);
        
        return_t parse_ret = wish_parse_transport_ip_port(transport, transport_len, &ip, &port);
        if (parse_ret == RET_SUCCESS) {
            /* Connect using the ip address, as the tranport clearly is an ip. */
            wish_open_connection(core, connection, &ip, port, false);
        }
        else {
            char host[WISH_MAX_TRANSPORT_LEN] = { 0 };
            parse_ret = wish_parse_transport_host_port(transport, transport_len, host, &port);
            if (parse_ret == RET_SUCCESS) {
                wish_open_connection_dns(core, connection, host, port, false);
            }
            else {
                WISHDEBUG(LOG_CRITICAL, "Cannot connect, the transport parsing fails for IP and hostname.");
                wish_close_connection(core, connection);
                return RET_FAIL;
            }
        }
    }
    
    return RET_SUCCESS;
}

void wish_close_parallel_connections(wish_core_t *core, void *_connection) {
    wish_connection_t *connection = (wish_connection_t *) _connection;
    
    if (connection->context_state != WISH_CONTEXT_CONNECTED) {
        return;
    }
    
    for (int i = 0; i < WISH_CONTEXT_POOL_SZ; i++) {
        wish_connection_t *c = &core->connection_pool[i];

        if (c == connection) {
            continue;
        }

        if (memcmp(c->luid, connection->luid, WISH_ID_LEN) == 0) {
            if (memcmp(c->ruid, connection->ruid, WISH_ID_LEN) == 0) {
                if (memcmp(c->rhid, connection->rhid, WISH_WHID_LEN) == 0) {
                    if (c->context_state == WISH_CONTEXT_CONNECTED) {
                        wish_close_connection(core, c);
                    }
               }
           }
       }
    }
}
      
