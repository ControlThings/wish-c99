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
#include "wish_core.h"
#include "wish_event.h"
#include "wish_debug.h"
#include "wish_rpc.h"
#include "wish_connection_mgr.h"
#include "wish_core_rpc.h"
#include "wish_identity.h"
#include "wish_utils.h"
#include "wish_core_signals.h"
#include "wish_dispatcher.h"


/* This task will be set up at the by of message_processor_task_init().
 * The OS task system will be notified from tcp_recv_cb, 
 * when new data becomes available. The Task system will then call this
 * function. Note that this not a thread, the system is still
 * single-tasking and not pre-empting. If you spend too much time in
 * this function, chaos will ensue. */
void wish_message_processor_task(wish_core_t* core, struct wish_event *e) {
    switch (e->event_type) {
    case WISH_EVENT_CONTINUE:
        WISHDEBUG(LOG_DEBUG,"Message processing started (continuation)\n\r");
        break;
    case WISH_EVENT_NEW_DATA:
        WISHDEBUG(LOG_DEBUG,"Message processing started (new data received)\n\r");
        break;
    case WISH_EVENT_NEW_CORE_CONNECTION:
        {
            wish_identity_t id;
            if (wish_identity_load(e->context->ruid, &id) != RET_SUCCESS) {
                WISHDEBUG(LOG_CRITICAL, "Could not load the identity, handling event WISH_EVENT_NEW_CORE_CONNECTION");
            }
            else {
                if (wish_identity_has_meta_unconfirmed_friend_request_flag(&id)) {
                    WISHDEBUG(LOG_CRITICAL, "Removing meta: { unconfirmedFriendRequest: true } flag for a connection which we had friendRequested, but not gotten a response under the lifetime of the friend req connection.");
                    wish_identity_remove_meta_outgoing_friend_request(core, e->context->ruid);
                    /* Signal friendRequqestee Accepted */
                    wish_core_signals_emit_string(core, "friendRequesteeAccepted");
                }
            }
            wish_identity_destroy(&id);
            
            e->context->context_state = WISH_CONTEXT_CONNECTED;
            wish_core_signals_emit_string(core, "connections");
            
            /* Check if we have parallel connections between the cores. 
             * Note that only one of the two cores can run this check, in order 
             * to avoid both ends running the check independently at the same 
             * time, and ending up closing different connections. 
             * This is determined by comparing the rhid "magnitudes".
             */
            uint8_t local_rhid[WISH_ID_LEN];
            wish_core_get_host_id(core, local_rhid);
            
            if (memcmp(e->context->rhid, local_rhid, WISH_ID_LEN) < 0) { /* Only if we have the bigger rhid, then we can run the check */
                wish_core_time_set_timeout(core, &wish_close_parallel_connections, e->context, 1);
            }
        }
        break;
    default:
        break;
    }

    wish_connection_t* connection = e->context;

    switch (e->event_type) {
    case WISH_EVENT_CONTINUE:
    case WISH_EVENT_NEW_DATA:
        wish_core_process_data(core, connection);
        break;
    case WISH_EVENT_NEW_CORE_CONNECTION:
        wish_core_send_peers_rpc_req(core, connection);
        break;
    case WISH_EVENT_REQUEST_CONNECTION_CLOSING:
        wish_close_connection(core, connection);
        break;
    default:
        WISHDEBUG(LOG_CRITICAL,"Uknown event type %d\n\r", e->event_type);
        break;
 
    }
}


