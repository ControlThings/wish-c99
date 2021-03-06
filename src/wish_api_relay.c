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
#include "wish_api_relay.h"
#include "utlist.h"
#include "wish_platform.h"
#include "string.h"
#include "wish_relay_client.h"

/**
 * relay.list
 * 
 * @param req
 * @param args
 */
void wish_api_relay_list(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = req->server->context;
    wish_app_entry_t* app = req->context;

    uint8_t buffer[WISH_PORT_RPC_BUFFER_SZ];
    
    bson bs;
    bson_init_buffer(&bs, buffer, WISH_PORT_RPC_BUFFER_SZ);
    bson_append_start_array(&bs, "data");
    
    wish_relay_client_t* relay = NULL;
    
    int i = 0;
    
    LL_FOREACH(core->relay_db, relay) {
        char index[21];
        BSON_NUMSTR(index, i);
        
        char host[RELAY_SERVER_HOST_MAX_LEN];
        wish_platform_snprintf(host, RELAY_SERVER_HOST_MAX_LEN, "%s:%d", relay->host, relay->port);
        
        bson_append_start_object(&bs, index);
        bson_append_string(&bs, "host", host);
        bson_append_bool(&bs, "connected", relay->curr_state == WISH_RELAY_CLIENT_WAIT);
        bson_append_finish_object(&bs);
    }
    
    bson_append_finish_array(&bs);
    bson_finish(&bs);

    if (bs.err) {
        rpc_server_error_msg(req, 305, "Failed writing bson.");
        return;
    }
    
    rpc_server_send(req, bson_data(&bs), bson_size(&bs));
}

/**
 * relay.add
 * 
 * @param req
 * @param args
 */
void wish_api_relay_add(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = req->server->context;
    wish_app_entry_t* app = req->context;

    bson_iterator it;
    bson_find_from_buffer(&it, args, "0");
    
    if ( BSON_STRING != bson_iterator_type(&it) ) {
        rpc_server_error_msg(req, 306, "Could not add relay. Expecting string parameter host: 92.12.33.221:40000.");
        return;
    }
    
    const char* addr = bson_iterator_string(&it);
    int addr_len = bson_iterator_string_len(&it); 

    wish_relay_client_add(core, addr);
    
    uint8_t buffer[WISH_PORT_RPC_BUFFER_SZ];
    
    bson bs;
    bson_init_buffer(&bs, buffer, WISH_PORT_RPC_BUFFER_SZ);
    bson_append_bool(&bs, "data", true);
    bson_append_finish_array(&bs);
    bson_finish(&bs);

    if (bs.err) {
        rpc_server_error_msg(req, 305, "Failed writing bson.");
        return;
    }
    
    rpc_server_send(req, bson_data(&bs), bson_size(&bs));
    
    wish_core_config_save(core);
}

void wish_api_relay_remove(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = req->server->context;
    wish_app_entry_t* app = req->context;

    bson_iterator it;
    bson_find_from_buffer(&it, args, "0");
    
    if ( BSON_STRING != bson_iterator_type(&it) ) {
        rpc_server_error_msg(req, 306, "Could not remove relay. Expecting string parameter host: 92.12.33.221:40000.");
        return;
    }
    
    const char* addr = bson_iterator_string(&it);
    int addr_len = bson_iterator_string_len(&it); 

    wish_relay_client_t ctx;
    if (wish_parse_transport_host_port(addr, addr_len, ctx.host, &ctx.port) != RET_SUCCESS) {
        rpc_server_error_msg(req, 307, "Could not remove relay. Could not parse host:port from the transport.");
        return;
    }
    
    
    wish_relay_client_t* relay;
    wish_relay_client_t* tmp;
    
    bool found = false;
    
    LL_FOREACH_SAFE(core->relay_db, relay, tmp) {
        if ( strncmp(relay->host, ctx.host, RELAY_SERVER_HOST_MAX_LEN) != 0 || relay->port != ctx.port ) { continue; }

        found = true;
        
        // close the underlying connection
        wish_relay_client_close(core, relay);
        
        LL_DELETE(core->relay_db, relay);
        wish_platform_free(relay);
    }
    
    uint8_t buffer[WISH_PORT_RPC_BUFFER_SZ];
    
    bson bs;
    bson_init_buffer(&bs, buffer, WISH_PORT_RPC_BUFFER_SZ);
    bson_append_bool(&bs, "data", found);
    bson_append_finish_array(&bs);
    bson_finish(&bs);

    if (bs.err) {
        rpc_server_error_msg(req, 305, "Failed writing bson.");
        return;
    }

    rpc_server_send(req, bson_data(&bs), bson_size(&bs));
    
    wish_core_config_save(core);
}
