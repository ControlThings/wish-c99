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
#include "wish_api_wld.h"
#include "utlist.h"
#include "wish_platform.h"
#include "string.h"
#include "wish_debug.h"
#include "wish_local_discovery.h"
#include "wish_connection_mgr.h"
#include "wish_core_signals.h"

/**
 * Wish Local Discovery
 *
 * App to core: { op: "wld.list", args: [ <Buffer> uid ], id: 5 }
 * Response core to App:
 *  { ack: 5, data: [item] }
 *
 *  item: {
 *   ruid: Buffer<>,
 *   rhid: Buffer<>,
 *   [pubkey: Buffer<>] // optional
 * }
 * 
 */
void wish_api_wld_list(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = (wish_core_t*) req->server->context;
    
    int buffer_len = WISH_PORT_RPC_BUFFER_SZ;
    uint8_t buffer[buffer_len];
    
    wish_ldiscover_t *db = wish_ldiscover_get(core);

    bson bs;
    bson_init_buffer(&bs, buffer, buffer_len);
    bson_append_start_array(&bs, "data");
    
    int i;
    int p = 0;
    for(i=0; i< WISH_LOCAL_DISCOVERY_MAX; i++) {
        if(db[i].occupied) {
            char index[21];
            BSON_NUMSTR(index, p);
            
            if (db[i].type == DISCOVER_TYPE_LOCAL) {
                bson_append_start_object(&bs, index);
                bson_append_string(&bs, "type", "local");
                bson_append_string(&bs, "alias", db[i].alias);
                bson_append_binary(&bs, "ruid", db[i].ruid, WISH_ID_LEN);
                bson_append_binary(&bs, "rhid", db[i].rhid, WISH_ID_LEN);
                bson_append_binary(&bs, "pubkey", db[i].pubkey, WISH_PUBKEY_LEN);
                if (db[i].claim) { bson_append_bool(&bs, "claim", true); }
                if (db[i].class) { bson_append_string(&bs, "class", db[i].class); }
                bson_append_finish_object(&bs);
            } else if (db[i].type == DISCOVER_TYPE_FRIEND_REQ) {
                bson_append_start_object(&bs, index);
                bson_append_string(&bs, "type", "friendReq");
                bson_append_string(&bs, "alias", db[i].alias);
                //bson_append_binary(&bs, "luid", db[i].luid, WISH_ID_LEN);
                bson_append_binary(&bs, "ruid", db[i].ruid, WISH_ID_LEN);
                bson_append_binary(&bs, "rhid", db[i].rhid, WISH_ID_LEN);
                bson_append_binary(&bs, "rsid", db[i].rsid, WISH_ID_LEN);
                bson_append_binary(&bs, "pubkey", db[i].pubkey, WISH_PUBKEY_LEN);
                bson_append_finish_object(&bs);
            }
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
 * Wish Local Discovery
 *
 * App to core: { op: "wld.announce", args: [], id: 5 }
 * Response core to App:
 *  { ack: 5, data: true }
 */
void wish_api_wld_announce(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = (wish_core_t*) req->server->context;

    wish_ldiscover_announce_all(core);
    
    int buffer_len = WISH_PORT_RPC_BUFFER_SZ;
    uint8_t buffer[buffer_len];

    bson bs;
    bson_init_buffer(&bs, buffer, buffer_len);
    bson_append_bool(&bs, "data", true);
    bson_finish(&bs);
    
    if (bs.err) {
        rpc_server_error_msg(req, 303, "Failed writing bson.");
        return;
    }
    
    rpc_server_send(req, bson_data(&bs), bson_size(&bs));
}

/**
 * Wish Local Discovery
 *
 * App to core: { op: "wld.list", args: [ <Buffer> uid ], id: 5 }
 * Response core to App:
 *  { ack: 5, data: [item] }
 *
 *  item: {
 *   ruid: Buffer<>,
 *   rhid: Buffer<>,
 *   [pubkey: Buffer<>] // optional
 * }
 * 
 */
void wish_api_wld_clear(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = (wish_core_t*) req->server->context;
    
    int buffer_len = WISH_PORT_RPC_BUFFER_SZ;
    uint8_t buffer[buffer_len];
    
    wish_ldiscover_clear(core);

    bson bs;
    bson_init_buffer(&bs, buffer, buffer_len);
    bson_append_bool(&bs, "data", true);
    bson_finish(&bs);
    
    if (bs.err) {
        rpc_server_error_msg(req, 303, "Failed writing bson.");
        return;
    }
    
    rpc_server_send(req, bson_data(&bs), bson_size(&bs));
}

/**
 * Wish Local Discovery
 *
 * App to core: { op: "wld.list", args: [ <Buffer> uid ], id: 5 }
 * Response core to App:
 *  { ack: 5, data: [item] }
 *
 *  item: {
 *   ruid: Buffer<>,
 *   rhid: Buffer<>,
 *   [pubkey: Buffer<>] // optional
 * }
 * 
 */
void wish_api_wld_friend_request(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = (wish_core_t*) req->server->context;
    
    int buffer_len = WISH_PORT_RPC_BUFFER_SZ;
    uint8_t buffer[buffer_len];

    bson_iterator it;
    
    bson_iterator_from_buffer(&it, args);
    
    if (bson_find_fieldpath_value("0", &it) != BSON_BINDATA) {
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

    if (bson_find_fieldpath_value("1", &it) != BSON_BINDATA) {
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

    if (bson_find_fieldpath_value("2", &it) != BSON_BINDATA) {
        rpc_server_error_msg(req, 307, "Argument 3 not Buffer.");
        return;
    }

    if (bson_iterator_bin_len(&it) != WISH_UID_LEN) {
        rpc_server_error_msg(req, 307, "Argument 3 not Buffer(32).");
        return;
    }

    /* Get the uid of identity to export, the uid is argument "0" in args */
    const uint8_t* rhid = bson_iterator_bin_data(&it);

    // now check if we have the wld details for this entry
    wish_ldiscover_t *db = wish_ldiscover_get(core);

    bool found = false;
    
    int i;
    for(i=0; i< WISH_LOCAL_DISCOVERY_MAX; i++) {
        if( db[i].occupied && 
                memcmp(&db[i].ruid, ruid, WISH_ID_LEN) == 0 &&
                memcmp(&db[i].rhid, rhid, WISH_ID_LEN) == 0) 
        {
            //WISHDEBUG(LOG_CRITICAL, "Found in slot %d", i);
            found = true;
            break;
        }
    }
    
    if(!found) {
        rpc_server_error_msg(req, 304, "Wld entry not found.");
        return;
    }
    
    // We have the friendrequestee's alias, ruid, pubkey and transports. We can now add the friend requestee to our contacts
    if (wish_identity_exists((uint8_t*) db[i].ruid) == 0) {
        /* Identity does not exist in our database */
        wish_identity_t new_id = { 0 };
        new_id.has_privkey = false;
        memcpy(new_id.uid, db[i].ruid, WISH_ID_LEN);
        strncpy(new_id.alias, db[i].alias, WISH_ALIAS_LEN);
        memcpy(new_id.pubkey, db[i].pubkey, WISH_PUBKEY_LEN);
        wish_platform_snprintf(new_id.transports[0], WISH_MAX_TRANSPORT_LEN, "%d.%d.%d.%d:%d", db[i].transport_ip.addr[0], db[i].transport_ip.addr[1], db[i].transport_ip.addr[2], db[i].transport_ip.addr[3], db[i].transport_port);        
        wish_save_identity_entry(&new_id);
        /* Flag the potential friend contact as an "unconfirmed friend request", and flag it also so that the wish core will not attempt normal connections to it for the time being.
         When the friend request connection is closed, if the the friend request is still not answered, remove the connect: false flag so that we may start attempting connections,
         whilst waiting for the remote end to some day accept the friend request. */
        wish_identity_add_meta_connect(core, (uint8_t*) db[i].ruid, false);
        wish_identity_add_meta_unconfirmed_friend_request(core, db[i].ruid);

        // signal 'identity' here, as the identity list has now been altered.
        wish_core_signals_emit_string(core, "identity");
    }

    wish_connection_t* connection = wish_connection_init(core, luid, ruid);
    connection->friend_req_connection = true;
    connection->friend_req_meta = db[i].meta;
    memcpy(connection->rhid, rhid, WISH_ID_LEN);
        
    uint8_t *ip = db[i].transport_ip.addr;
    
    //WISHDEBUG(LOG_CRITICAL, "Will start a friend req connection to: %u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);

    wish_open_connection(core, connection, &(db[i].transport_ip), db[i].transport_port, false);

    bson bs;
    bson_init_buffer(&bs, buffer, buffer_len);
    bson_append_bool(&bs, "data", true);
    bson_finish(&bs);
    
    if (bs.err) {
        rpc_server_error_msg(req, 303, "Failed writing bson.");
        return;
    }
    
    rpc_server_send(req, bson_data(&bs), bson_size(&bs));
}
