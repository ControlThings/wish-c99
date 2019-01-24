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
#include "wish_api_connections.h"
#include "wish_connection.h"
#include "wish_connection_mgr.h"
#include "utlist.h"

#include "wish_debug.h"
#include "bson_visit.h"

/**
 * connections.list
 * 
 * @param req
 * @param args
 */
void wish_api_connections_list(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = (wish_core_t*) req->server->context;
    
    int buffer_len = WISH_PORT_RPC_BUFFER_SZ;
    uint8_t buffer[buffer_len];
    
    wish_connection_t *db = wish_core_get_connection_pool(core);
    
    bson bs;
    bson_init_buffer(&bs, buffer, buffer_len);
    bson_append_start_array(&bs, "data");
    
    int i;
    int p = 0;
    for(i=0; i< WISH_CONTEXT_POOL_SZ; i++) {
        if(db[i].context_state != WISH_CONTEXT_FREE) {
            if (db[i].curr_protocol_state != PROTO_STATE_WISH_RUNNING) { continue; }
            
            char index[21];
            BSON_NUMSTR(index, p);
            
            //bson_append_start_object(&bs, nbuf);
            bson_append_start_object(&bs, index);
            bson_append_int(&bs, "cid", i);
            bson_append_int(&bs, "internalCid",db[i].connection_id);
            bson_append_binary(&bs, "luid", db[i].luid, WISH_ID_LEN);
            bson_append_binary(&bs, "ruid", db[i].ruid, WISH_ID_LEN);
            bson_append_binary(&bs, "rhid", db[i].rhid, WISH_ID_LEN);
            //bson_append_bool(&bs, "online", true);
            bson_append_bool(&bs, "outgoing", db[i].outgoing);
            bson_append_bool(&bs, "relay", db[i].via_relay);
            if (db[i].friend_req_connection) {
                bson_append_bool(&bs, "friendRequest", db[i].friend_req_connection);
            }
            //bson_append_bool(&bs, "authenticated", true);
            /*
            bson_append_start_object(&bs, "transport");
            bson_append_string(&bs, "type", "tcp");
            bson_append_string(&bs, "localAddress", "5.5.5.5:5555");
            bson_append_string(&bs, "remoteAddress", "6.6.6.6:6666");
            bson_append_finish_object(&bs);
            */
            bson_append_finish_object(&bs);
            p++;
        }
    }

    bson_append_finish_array(&bs);
    bson_finish(&bs);
    
    if (bs.err) {
        rpc_server_error_msg(req, 303, "Failed writing bson.");
        return;
    }
    
    rpc_server_send(req, bson_data(&bs), bson_size(&bs));
}

/** 
 * Client callback function for 'peers' RPC request send by core RPC
 * 
 *     data: {
 *         rsid: Buffer(32),
 *         protocol: string
 *         online: boolean
 *     }
 * 
 * client to a remote core 
 */
static void rpc_callback(rpc_client_req* req, void* context, const uint8_t* payload, size_t payload_len) {
    wish_connection_t* connection = context;
    wish_core_t* core = req->client->context;
    rpc_server_req* sreq = req->passthru_ctx;
    
    //WISHDEBUG(LOG_CRITICAL, "CoreRPC: got respose to %d", req->id);
    
    //bson_visit("CoreRPC: payload:", payload);
    
    int buf_len = WISH_PORT_RPC_BUFFER_SZ;
    uint8_t buf[buf_len];

    bson bs;
    bson_init_buffer(&bs, buf, buf_len);
    
    bson_iterator it;
    bson_find_from_buffer(&it, payload, "data");
    bson_append_element(&bs, "data", &it);
    
    //bson_append_bool(&bs, "data", true);
    bson_finish(&bs);
    
    if (bson_find_from_buffer(&it, payload, "err") == BSON_INT) {
        rpc_server_error(sreq, bson_data(&bs), bson_size(&bs));
        return;
    }

    if (bson_find_from_buffer(&it, payload, "sig") == BSON_INT) {
        rpc_server_emit(sreq, bson_data(&bs), bson_size(&bs));
        return;
    }
    
    rpc_server_send(sreq, bson_data(&bs), bson_size(&bs));
}

void wish_api_connections_request(rpc_server_req* req, const uint8_t* args) {
    // [{ luid, ruid, rhid }, op, args]
    wish_core_t* core = (wish_core_t*) req->server->context;
    
    bson_iterator it;
    
    bson_iterator_from_buffer(&it, args);
    
    if (bson_find_fieldpath_value("0.luid", &it) != BSON_BINDATA) {
        rpc_server_error_msg(req, 307, "Argument 1 not Buffer.");
        return;
    }
    
    if (bson_iterator_bin_len(&it) != WISH_UID_LEN) {
        rpc_server_error_msg(req, 307, "Argument 1 not Buffer(32).");
        return;
    }
    
    /* Get the uid of identity to export, the uid is argument "0" in args */
    const uint8_t* luid = bson_iterator_bin_data(&it);

    bson_iterator_from_buffer(&it, args);

    if (bson_find_fieldpath_value("0.ruid", &it) != BSON_BINDATA) {
        rpc_server_error_msg(req, 307, "Argument 2 not Buffer.");
        return;
    }

    if (bson_iterator_bin_len(&it) != WISH_UID_LEN) {
        rpc_server_error_msg(req, 307, "Argument 2 not Buffer(32).");
        return;
    }

    /* Get the uid of identity to export, the uid is argument "0" in args */
    const uint8_t* ruid = bson_iterator_bin_data(&it);

    bson_iterator_from_buffer(&it, args);

    if (bson_find_fieldpath_value("0.rhid", &it) != BSON_BINDATA) {
        rpc_server_error_msg(req, 307, "Argument 3 not Buffer.");
        return;
    }

    if (bson_iterator_bin_len(&it) != WISH_UID_LEN) {
        rpc_server_error_msg(req, 307, "Argument 3 not Buffer(32).");
        return;
    }

    /* Get the uid of identity to export, the uid is argument "0" in args */
    const uint8_t* rhid = bson_iterator_bin_data(&it);

    bson_iterator_from_buffer(&it, args);

    if (bson_find_fieldpath_value("1", &it) != BSON_STRING) {
        rpc_server_error_msg(req, 307, "Argument 2 not String.");
        return;
    }
    
    /* Get the op string */
    const uint8_t* op = bson_iterator_string(&it);

    bson_iterator_from_buffer(&it, args);

    if (bson_find_fieldpath_value("2", &it) != BSON_ARRAY) {
        rpc_server_error_msg(req, 307, "Argument 3, args not Array.");
        return;
    }
    
    int abuf_len = 1024;
    char abuf[abuf_len];
    
    bson ba;
    bson_init_buffer(&ba, abuf, abuf_len);
    bson_append_string(&ba, "op", op);
    bson_append_element(&ba, "args", &it);
    bson_append_int(&ba, "id", 0);
    bson_finish(&ba);
    
    wish_connection_t* connection = wish_core_lookup_connected_ctx_by_luid_ruid_rhid(core, luid, ruid, rhid);
    
    if (connection == NULL) {
        rpc_server_error_msg(req, 39, "Connection not found.");
        return;
    }
    
    if (ba.err) {
        rpc_server_error_msg(req, 39, "Args copy failed.");
        return;
    }
    
    rpc_client_req* mreq = rpc_client_request(core->core_rpc_client, &ba, rpc_callback, connection);

    if (mreq == NULL) {
        rpc_server_error_msg(req, 39, "wish rpc client error");
        return;
    }
    
    mreq->passthru_ctx = req;

    size_t buf_len = abuf_len+256;
    uint8_t buf[buf_len];
    
    bson b;
    bson_init_buffer(&b, buf, buf_len);
    bson_append_bson(&b, "req", &ba);
    bson_finish(&b);
    
    if (b.err) {
        rpc_server_error_msg(req, 39, "Building req failed.");
        return;
    }
    
    wish_core_send_message(core, connection, bson_data(&b), bson_size(&b));
}

/**
 * connections.disconnect
 * 
 * @param req
 * @param args
 */
void wish_api_connections_disconnect(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = (wish_core_t*) req->server->context;
    
    int buffer_len = WISH_PORT_RPC_BUFFER_SZ;
    uint8_t buffer[buffer_len];
    
    wish_connection_t *db = wish_core_get_connection_pool(core);

    bson_iterator it;
    bson_find_from_buffer(&it, args, "0");
    
    if (bson_iterator_type(&it) == BSON_INT) {

        int idx = bson_iterator_int(&it);
        
        // FIXME using the index given by user without checking validity is highly dangerous!
        wish_close_connection(core, &db[idx]);

        bson bs;

        bson_init_buffer(&bs, buffer, buffer_len);
        bson_append_bool(&bs, "data", true);
        bson_finish(&bs);

        if(bs.err != 0) {
            rpc_server_error_msg(req, 344, "Failed writing reponse.");
            return;
        }
        
        rpc_server_send(req, bson_data(&bs), bson_size(&bs));
    } else {
        rpc_server_error_msg(req, 343, "Invalid argument. Int index.");
    }
}

/**
 * connections.disconnectAll
 * 
 * @param req
 * @param args
 */
void wish_api_connections_disconnect_all(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = req->server->context;
    
    wish_connections_close_all(core);
    
    int buf_len = 128;
    char buf[buf_len];
    
    bson bs;
    bson_init_buffer(&bs, buf, buf_len);
    bson_append_bool(&bs, "data", true);
    bson_finish(&bs);
    
    rpc_server_send(req, bson_data(&bs), bson_size(&bs));
}

/**
 *  connections.checkConnections
 */
void wish_api_connections_check_connections(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = (wish_core_t*) req->server->context;
    
    int buffer_len = WISH_PORT_RPC_BUFFER_SZ;
    uint8_t buffer[buffer_len];
    
    wish_connections_check(core);
    
    bson bs;

    bson_init_buffer(&bs, buffer, buffer_len);
    bson_append_bool(&bs, "data", true);
    bson_finish(&bs);

    if(bs.err != 0) {
        rpc_server_error_msg(req, 344, "Failed writing reponse.");
        return;
    }
    rpc_server_send(req, bson_data(&bs), bson_size(&bs));
}

/**
 * connections.apps
 * 
 * @param req
 * @param args
 */
void wish_api_connections_apps(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = (wish_core_t*) req->server->context;
    
    bson_iterator it;
    
    bson_iterator_from_buffer(&it, args);
    
    if (bson_find_fieldpath_value("0.luid", &it) != BSON_BINDATA) {
        rpc_server_error_msg(req, 307, "Argument 1 not Buffer.");
        return;
    }
    
    if (bson_iterator_bin_len(&it) != WISH_UID_LEN) {
        rpc_server_error_msg(req, 307, "Argument 1 not Buffer(32).");
        return;
    }
    
    /* Get the uid of identity to export, the uid is argument "0" in args */
    const uint8_t* luid = bson_iterator_bin_data(&it);

    bson_iterator_from_buffer(&it, args);

    if (bson_find_fieldpath_value("0.ruid", &it) != BSON_BINDATA) {
        rpc_server_error_msg(req, 307, "Argument 2 not Buffer.");
        return;
    }

    if (bson_iterator_bin_len(&it) != WISH_UID_LEN) {
        rpc_server_error_msg(req, 307, "Argument 2 not Buffer(32).");
        return;
    }

    /* Get the uid of identity to export, the uid is argument "0" in args */
    const uint8_t* ruid = bson_iterator_bin_data(&it);

    bson_iterator_from_buffer(&it, args);

    if (bson_find_fieldpath_value("0.rhid", &it) != BSON_BINDATA) {
        rpc_server_error_msg(req, 307, "Argument 3 not Buffer.");
        return;
    }

    if (bson_iterator_bin_len(&it) != WISH_UID_LEN) {
        rpc_server_error_msg(req, 307, "Argument 3 not Buffer(32).");
        return;
    }

    /* Get the uid of identity to export, the uid is argument "0" in args */
    const uint8_t* rhid = bson_iterator_bin_data(&it);
    
    wish_connection_t* connection = wish_core_lookup_connected_ctx_by_luid_ruid_rhid(core, luid, ruid, rhid);
    
    if (connection == NULL) {
        rpc_server_error_msg(req, 39, "Connection not found.");
        return;
    }

    int buffer_len = WISH_PORT_RPC_BUFFER_SZ;
    uint8_t buffer[buffer_len];
    
    bson bs;

    bson_init_buffer(&bs, buffer, buffer_len);
    bson_append_start_object(&bs, "data");
    
    wish_remote_app* app;
    
    int i = 0;
    char index[21];
    
    LL_FOREACH(connection->apps, app) {
        BSON_NUMSTR(index, i++);
        
        bson_append_start_object(&bs, index);
        bson_append_string(&bs, "name", app->name);
        bson_append_binary(&bs, "rsid", app->rsid, WISH_WSID_LEN);
        bson_append_start_array(&bs, "protocols");
        bson_append_string(&bs, "0", app->protocol);
        bson_append_finish_array(&bs);
        bson_append_finish_object(&bs);
    }
    
    bson_append_finish_object(&bs);
    bson_finish(&bs);

    if(bs.err != 0) {
        rpc_server_error_msg(req, 344, "Failed writing response.");
        return;
    }
    
    rpc_server_send(req, bson_data(&bs), bson_size(&bs));

}
