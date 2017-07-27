#include "wish_api_identity.h"
#include "utlist.h"
#include "wish_io.h"
#include "wish_core_signals.h"
#include "wish_local_discovery.h"
#include "wish_connection_mgr.h"
#include "wish_service_registry.h"
#include "core_service_ipc.h"
#include "wish_relationship.h"
#include "wish_dispatcher.h"
#include "wish_platform.h"
#include "wish_debug.h"
#include "string.h"
#include "bson.h"
#include "bson_visit.h"


/* This is the Call-back function invoked by the core's "app" RPC
 * server, when identity.export is received from a Wish app 
 *
 * identity.export('342ef67c822662174e67689b8b1f1ef761c8085129561372adeb9ccf6ec30c86')
 * RPC app to core { op: 'identity.export',
 *   args: [
 *   '342ef67c822662174e67689b8b1f1ef761c8085129561372adeb9ccf6ec30c86'
 *   ],
 *   id: 3 }
 * Core to app: { ack: 3,
 *       data:
 *       'H4sIAAAAAAAAA61TPW8...2d2b3GF2jAfwBaWrGAmsEAAA='
 *       }
 *
 */
void wish_api_identity_export(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = req->server->context;
    wish_app_entry_t* app = req->context;
    
    
    bson_iterator it;
    bson_find_from_buffer(&it, args, "0");
    
    if ( bson_iterator_type(&it) != BSON_BINDATA || bson_iterator_bin_len(&it) != WISH_UID_LEN ) {
        WISHDEBUG(LOG_CRITICAL, "Could not get argument[0]: uid, expecting Buffer(32)");
        wish_rpc_server_error(req, 8, "Missing export uid argument, expecting Buffer(32)");
        return;
    }

    const uint8_t* uid = bson_iterator_bin_data(&it);
    
    wish_identity_t id;
    
    if ( RET_SUCCESS != wish_identity_load(uid, &id) ) {
        wish_rpc_server_error(req, 343, "Failed to load identity.");
        return;
    }

    char buf_base[WISH_PORT_RPC_BUFFER_SZ];
    
    bin buf;
    buf.base = buf_base;
    buf.len = WISH_PORT_RPC_BUFFER_SZ;
    
    if ( RET_SUCCESS != wish_identity_export(core, &id, &buf) ) {
        wish_rpc_server_error(req, 92, "Internal export failed.");
        return;
    }
    
    bson bs;
    bson_init_with_data(&bs, buf.base);

    char buf_base2[WISH_PORT_RPC_BUFFER_SZ];
    
    bin buf2;
    buf2.base = buf_base2;
    buf2.len = WISH_PORT_RPC_BUFFER_SZ;
    
    bson b;
    bson_init_buffer(&b, buf2.base, buf2.len);
    bson_append_bson(&b, "data", &bs);
    bson_finish(&b);
    
    wish_rpc_server_send(req, bson_data(&b), bson_size(&b));
}

/**
 * Identity import RPC handler
 *
 *     { alias: String,
 *       pubkey: Buffer(32)
 */
void wish_api_identity_import(rpc_server_req* req, const uint8_t* args) {
    WISHDEBUG(LOG_DEBUG, "Core app RPC: identity_import");

    
    bson b;
    bson_init_with_data(&b, args);
    
    bson_iterator it;
    
    bson_find(&it, &b, "0");
    
    if ( bson_iterator_type(&it) != BSON_BINDATA) {
        WISHDEBUG(LOG_CRITICAL, "Expected argument 1 to be Buffer");
        wish_rpc_server_error(req, 76, "Expected argument 1 to be Buffer");
        return;
    }

    bson_visit("import args:", bson_iterator_bin_data(&it));
    
    bson i;
    bson_init_with_data(&i, bson_iterator_bin_data(&it));
    
    wish_identity_t id;
    memset(&id, 0, sizeof (wish_identity_t));
    if (wish_identity_from_bson(&id, &i)) {
        /* ...it failed somehow.. */
        WISHDEBUG(LOG_CRITICAL, "There was an error when populating the new id struct");
        wish_rpc_server_error(req, 76, "There was an error when populating the new id struct");
        return;
    }

    if (wish_identity_exists(id.uid)>0) {
        // it already exists, bail!
        wish_rpc_server_error(req, 202, "Identity already exists.");
        return;
    }
    
    /* The identity to be imported seems valid! */

    /* Save the new identity to database */
    //wish_save_identity_entry_bson(new_id_doc);
    int ret = wish_save_identity_entry(&id);
    
    if( ret != 0 ) {
        WISHDEBUG(LOG_CRITICAL, "wish_save_identity_entry return other than success (0): %d", ret);
        wish_rpc_server_error(req, 201, "Too many identities.");
        return;
    }

    int buffer_len = WISH_PORT_RPC_BUFFER_SZ;
    uint8_t buffer[buffer_len];
    
    bson bs;
    bson_init_buffer(&bs, buffer, buffer_len);

    bson_append_start_object(&bs, "data");
    bson_append_string(&bs, "alias", id.alias);
    bson_append_binary(&bs, "uid", id.uid, WISH_UID_LEN);
    bson_append_finish_object(&bs);
    bson_finish(&bs);
    
    wish_rpc_server_send(req, bson_data(&bs), bson_size(&bs));
}

/* This is the Call-back function invoked by the core's "app" RPC
 * server, when identity.list is received from a Wish app 
 *
 *  identity.list()
 *  RPC app to core { op: 'identity.list', args: [], id: 2 }
 *  Core to app: { ack: 2,
 *    data: 
 *       [ { alias: 'Jan2',
 *           id: '342ef67c822662174e67689b8b1f1ef761c8085129561372adeb9ccf6ec30c86',
 *           pubkey:'62d5b302ef33ee27bb52781b1b3946b04f856e5cf964f6418770e859338268f7',
 *           privkey: true,
 *           hosts: [Object],
 *           contacts: [Object],
 *           transports: [Object],
 *           trust: null },
 *
 *       ]
 *
 */
void wish_api_identity_list(rpc_server_req* req, const uint8_t* args) {
    
    int num_uids_in_db = wish_get_num_uid_entries();
    wish_uid_list_elem_t uid_list[num_uids_in_db];
    int num_uids = wish_load_uid_list(uid_list, num_uids_in_db);

    bson bs; 
    bson_init(&bs);
    bson_append_start_array(&bs, "data");
    
    int i = 0;
    for (i = 0; i < num_uids; i++) {
        char num_str[8];
        bson_numstr(num_str, i);
        bson_append_start_object(&bs, num_str);
        
        wish_identity_t identity;

        if ( RET_SUCCESS != wish_identity_load(uid_list[i].uid, &identity) ) {
            WISHDEBUG(LOG_CRITICAL, "Could not load identity");
            wish_rpc_server_error(req, 997, "Could not load identity");
        }

        bson_append_binary(&bs, "uid", identity.uid, WISH_UID_LEN);
        bson_append_string(&bs, "alias", identity.alias);
        bson_append_bool(&bs, "privkey", identity.has_privkey);
        
        bson_append_finish_object(&bs);
    }
    
    bson_append_finish_array(&bs);
    bson_finish(&bs);
    
    if (bs.err) {
        WISHDEBUG(LOG_CRITICAL, "BSON error in identity_list_handler");
        wish_rpc_server_error(req, 997, "BSON error in identity_list_handler");
    } else {
        wish_rpc_server_send(req, bson_data(&bs), bson_size(&bs));
    }
    
    bson_destroy(&bs);
}

/**
 * identity.get
 *
 * App to core: { op: "identity.get", args: [ Buffer(32) uid ], id: 5 }
 * Response core to App:
 *  { ack: 5, data: {
 *          alias: "Moster Greta",
 *          uid: <binary buffer containing the new wish user id>,
 *          privkey: true,
 *          pubkey: Buffer
 *      }
 *  }
 *
 *  Note that privkey is always returned as 'true' when doing
 *  identity.create (An identity creation always involves creation of
 *  private key and public key)
 */
void wish_api_identity_get(rpc_server_req* req, const uint8_t* args) {
    WISHDEBUG(LOG_DEBUG, "In identity_get_handler");

    bson bs; 
    bson_init(&bs);
    bson_append_start_object(&bs, "data");
    
    bson_iterator it;
    bson_iterator_from_buffer(&it, args);
    
    if (bson_find_fieldpath_value("0", &it) != BSON_BINDATA) {
        wish_rpc_server_error(req, 308, "Argument 1 must be Buffer");
        return;
    }
    
    if (bson_iterator_bin_len(&it) != WISH_UID_LEN) {
        wish_rpc_server_error(req, 308, "Argument 1 must be Buffer(32)");
        return;
    }
    
    const uint8_t *arg_uid = bson_iterator_bin_data(&it);

    wish_identity_t identity;

    if ( RET_SUCCESS != wish_identity_load(arg_uid, &identity) ) {
        WISHDEBUG(LOG_CRITICAL, "Could not load identity");
        wish_rpc_server_error(req, 997, "Could not load identity");
    }

    bson_append_binary(&bs, "uid", identity.uid, WISH_UID_LEN);
    bson_append_string(&bs, "alias", identity.alias);
    bson_append_bool(&bs, "privkey", identity.has_privkey);
    bson_append_binary(&bs, "pubkey", identity.pubkey, WISH_PUBKEY_LEN);

    // TODO: Support multiple transports
    if ( strnlen(&identity.transports[0][0], 64) % 64 != 0 ) {
        bson_append_start_array(&bs, "hosts");
        bson_append_start_object(&bs, "0");
        bson_append_start_array(&bs, "transports");
        bson_append_string(&bs, "0", &identity.transports[0][0]);
        bson_append_finish_array(&bs);
        bson_append_finish_object(&bs);
        bson_append_finish_array(&bs);
    }
            
    bson_append_finish_object(&bs);
    bson_finish(&bs);

    if (bs.err) {
        WISHDEBUG(LOG_CRITICAL, "BSON error in identity_get_handler");
        wish_rpc_server_error(req, 997, "BSON error in identity_get_handler");
    } else {
        wish_rpc_server_send(req, bs.data, bson_size(&bs));
    }
    bson_destroy(&bs);
}

/**
 * identity.create
 *
 * App to core: { op: "identity.create", args: [ "Moster Greta" ], id: 5 }
 * Response core to App:
 *  { ack: 5, data: {
 *          alias: "Moster Greta",
 *          uid: <binary buffer containing the new wish user id>,
 *          privkey: true;
 *      }
 *  }
 *
 *  Note that privkey is always returned as 'true' when doing
 *  identity.create (An identity creation always involves creation of
 *  private key and public key)
 */
void wish_api_identity_create(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = (wish_core_t*) req->server->context;
    
    /* Get the new identity's alias, it is element 0 of array 'args' */
    bson_iterator it;
    bson_iterator_from_buffer(&it, args);
    
    if ( bson_find_fieldpath_value("0", &it) != BSON_STRING ) {
        wish_rpc_server_error(req, 309, "Argument 1 must be string");
        return;
    }
    
    const char *alias_str = bson_iterator_string(&it);

    WISHDEBUG(LOG_DEBUG, "Core app RPC: identity_create for alias %s", alias_str);

    /* Create the identity */
    wish_identity_t id;
    wish_create_local_identity(&id, alias_str);
    int ret = wish_save_identity_entry(&id);

    
    int buf_len = 128;
    uint8_t buf[buf_len];
    
    bson bs;
    bson_init_buffer(&bs, buf, buf_len);
    bson_append_binary(&bs, "0", id.uid, WISH_UID_LEN);
    bson_finish(&bs);

    // pass to identity get handler with uid as parameter
    wish_api_identity_get(req, (char*) bson_data(&bs));
    
    WISHDEBUG(LOG_CRITICAL, "Starting to advertize the new identity");
    wish_core_update_identities(core);

    wish_ldiscover_advertize(core, id.uid);
    wish_report_identity_to_local_services(core, &id, true);

    wish_core_signals_emit_string(core, "identity");
}

/**
 * identity.remove
 *
 * App to core: { op: "identity.remove", args: [ <Buffer> uid ], id: 5 }
 * Response core to App:
 *  { ack: 5, data: true }
 *
 *  Note that privkey is always returned as 'true' when doing
 *  identity.create (An identity creation always involves creation of
 *  private key and public key)
 */
void wish_api_identity_remove(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = (wish_core_t*) req->server->context;
    
    int buffer_len = WISH_PORT_RPC_BUFFER_SZ;
    uint8_t buffer[buffer_len];

    bson_iterator it;
    bson_find_from_buffer(&it, args, "0");

    if(bson_iterator_type(&it) == BSON_BINDATA && bson_iterator_bin_len(&it) == WISH_ID_LEN) {

        /* Get the uid of identity to export, the uid is argument "0" in
         * args */
        uint8_t *luid = 0;
        luid = (uint8_t *)bson_iterator_bin_data(&it);

        wish_identity_t id_to_remove;
        if (wish_identity_load(luid, &id_to_remove) == RET_SUCCESS) {
            wish_report_identity_to_local_services(core, &id_to_remove, false);
        }
        
        int res = wish_identity_remove(core, luid);

        bson bs;

        bson_init_buffer(&bs, buffer, buffer_len);
        bson_append_bool(&bs, "data", res == 1 ? true : false);
        bson_finish(&bs);

        if (bs.err != 0) {
            wish_rpc_server_error(req, 344, "Failed writing reponse.");
            return;
        }

        wish_core_update_identities(core);
        wish_rpc_server_send(req, bson_data(&bs), bson_size(&bs));
        
        wish_core_signals_emit_string(core, "identity");
    } else {
        wish_rpc_server_error(req, 343, "Invalid argument. Expecting 32 byte bin data.");
        return;
    }
}

/**
 * identity.sign
 *
 * App to core: { op: "identity.sign", args: [ <Buffer> uid, <Buffer> hash ], id: 5 }
 */
void wish_api_identity_sign(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = (wish_core_t*) req->server->context;
    
    int buffer_len = WISH_PORT_RPC_BUFFER_SZ;
    uint8_t buffer[buffer_len];

    // allocate space for signature and privkey
    uint8_t signature_base[ED25519_SIGNATURE_LEN];
    
    bin signature;
    signature.base = signature_base;
    signature.len = ED25519_SIGNATURE_LEN;

    bson_iterator it;
    bson_find_from_buffer(&it, args, "0");
    
    if(bson_iterator_type(&it) != BSON_BINDATA || bson_iterator_bin_len(&it) != WISH_ID_LEN) {
        wish_rpc_server_error(req, 345, "Invalid uid.");
        return;
    }

    uint8_t* luid = (uint8_t *)bson_iterator_bin_data(&it);
    
    wish_identity_t uid;
    
    // check if we can make a signature with this identity
    if (wish_identity_load(luid, &uid) != RET_SUCCESS) {
        WISHDEBUG(LOG_CRITICAL, "Could not load identity");
        wish_rpc_server_error(req, 345, "Could not load identity.");
        return;
    }
    
    bin claim;
    claim.base = NULL;
    claim.len = 0;

    bson_find_from_buffer(&it, args, "2");

    if(bson_iterator_type(&it) == BSON_BINDATA && bson_iterator_bin_len(&it) >= 5 && bson_iterator_bin_len(&it) <= 512 ) {
        claim.base = (char*) bson_iterator_bin_data(&it);
        claim.len = bson_iterator_bin_len(&it);
        WISHDEBUG(LOG_CRITICAL, "Sign with claim. %p %i", claim.base, claim.len);
    }
    
    bson_find_from_buffer(&it, args, "1");
    
    if(bson_iterator_type(&it) == BSON_BINDATA && bson_iterator_bin_len(&it) >= 32 && bson_iterator_bin_len(&it) <= 64 ) {
        // sign hash
        char hash[64];
        int hash_len = bson_iterator_bin_len(&it);

        memcpy(hash, bson_iterator_bin_data(&it), hash_len);

        //wish_debug_print_array(LOG_DEBUG, signature, ED25519_SIGNATURE_LEN);

        bson bs;

        bson_init_buffer(&bs, buffer, buffer_len);
        bson_append_binary(&bs, "data", signature.base, ED25519_SIGNATURE_LEN);
        bson_finish(&bs);

        if(bs.err != 0) {
            wish_rpc_server_error(req, 344, "Failed writing reponse.");
            return;
        }

        wish_rpc_server_send(req, bson_data(&bs), bson_size(&bs));
    } else if (bson_iterator_type(&it) == BSON_OBJECT) {
        // sign object { data: Buffer(n) }

        bson b;

        bson_init_buffer(&b, buffer, buffer_len);
        bson_append_start_object(&b, "data");
        
        bson_iterator_from_buffer(&it, args);
        
        if ( bson_find_fieldpath_value("1.data", &it) != BSON_BINDATA ) {
            WISHDEBUG(LOG_CRITICAL, "1.data not bin data");
            
            wish_rpc_server_error(req, 345, "Second arg object does not have { data: <Buffer> }.");
            return;
        }

        // copy the data blob to response
        bin data;
        data.base = (char*) bson_iterator_bin_data(&it);
        data.len = bson_iterator_bin_len(&it);
        
        bson_append_binary(&b, "data", data.base, data.len);
        
        bson_append_field_from_iterator(&it, &b);

        bson_iterator_from_buffer(&it, args);
        
        if ( bson_find_fieldpath_value("1.meta", &it) != BSON_EOO ) {
            WISHDEBUG(LOG_CRITICAL, "1.meta");
            bson_append_field_from_iterator(&it, &b);
        }

        bson_iterator_from_buffer(&it, args);

        bson_append_start_array(&b, "signatures");

        char index[21];
        int i = 0;
        
        // copy signatures already present
        if ( bson_find_fieldpath_value("1.signatures.0", &it) != BSON_EOO ) {
            
            do {
                BSON_NUMSTR(index, i++);
                WISHDEBUG(LOG_CRITICAL, "1.signatures.0 already present, should be copied. %i", i);
                bson_append_element(&b, index, &it);
            } while ( bson_iterator_next(&it) != BSON_EOO );
        }
        
        // add signature by uid
        wish_identity_sign(core, &uid, &data, &claim, &signature);
        
        BSON_NUMSTR(index, i++);

        bson_append_start_object(&b, index);
        bson_append_string(&b, "algo", "sha256-ed25519");
        bson_append_binary(&b, "uid", luid, WISH_UID_LEN);
        bson_append_binary(&b, "sign", signature.base, ED25519_SIGNATURE_LEN);
        if (claim.base != NULL && claim.len > 0) {
            bson_append_binary(&b, "claim", claim.base, claim.len);
        }
        
        bson_append_finish_object(&b);
        bson_append_finish_array(&b);
        
        bson_append_finish_object(&b);
        bson_finish(&b);

        if(b.err != 0) {
            wish_rpc_server_error(req, 344, "Failed writing reponse.");
            return;
        }
        
        wish_rpc_server_send(req, bson_data(&b), bson_size(&b));
        return;
    } else {
        wish_rpc_server_error(req, 345, "Second arg not valid hash or object.");
        return;
    }
}

/**
 * identity.verify
 *
 * args: BSON(
 *   [ { 
 *     data: <Buffer>,
 *     meta: <Buffer>,
 *     signatures: [{ 
 *       uid: Buffer,
 *       sign: Buffer,
 *       claim?: Buffer }] ] })
 * 
 * return: BSON(
 *   [ { 
 *     data: <Buffer>,
 *     meta: <Buffer>,
 *     signatures: [{ 
 *       uid: Buffer,
 *       sign: bool | null, // bool: verification result, null: unable to verify signature
 *       claim?: Buffer }] ] })
 */
void wish_api_identity_verify(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = (wish_core_t*) req->server->context;
    
    uint8_t buffer[WISH_PORT_RPC_BUFFER_SZ];

    bson_iterator it;
    bson_find_from_buffer(&it, args, "0");
    
    if(bson_iterator_type(&it) != BSON_OBJECT) {
        wish_rpc_server_error(req, 345, "Expected object");
        return;
    }

    bson b;

    bson_init_buffer(&b, buffer, WISH_PORT_RPC_BUFFER_SZ);
    bson_append_start_object(&b, "data");

    bson_iterator_from_buffer(&it, args);

    if ( bson_find_fieldpath_value("0.data", &it) != BSON_BINDATA ) {
        WISHDEBUG(LOG_CRITICAL, "0.data not bin data");

        wish_rpc_server_error(req, 345, "Object does not have { data: <Buffer> }.");
        return;
    }

    // copy the data blob to response
    bin data;
    data.base = (char*) bson_iterator_bin_data(&it);
    data.len = bson_iterator_bin_len(&it);

    bson_append_binary(&b, "data", data.base, data.len);

    bson_append_field_from_iterator(&it, &b);

    bson_iterator_from_buffer(&it, args);

    if ( bson_find_fieldpath_value("0.meta", &it) != BSON_EOO ) {
        WISHDEBUG(LOG_CRITICAL, "0.meta");
        bson_append_field_from_iterator(&it, &b);
    }

    bson_iterator_from_buffer(&it, args);

    bson_append_start_array(&b, "signatures");

    char index[21];
    int i = 0;

    // parse signature array
    if ( bson_find_fieldpath_value("0.signatures.0", &it) == BSON_OBJECT ) {
        do {
            BSON_NUMSTR(index, i++);
            bson_append_start_object(&b, index);
            
            WISHDEBUG(LOG_CRITICAL, "0.signatures.0 already present, should be verified. %i %s", bson_iterator_type(&it), bson_iterator_key(&it));
            bson obj;
            bson_iterator_subobject(&it, &obj);
            bson_iterator sit;
            bson_iterator_init(&sit, &obj);
            
            const char* uid = NULL;
            
            bin claim;
            memset(&claim, 0, sizeof(bin));
            
            bin signature;
            memset(&signature, 0, sizeof(bin));
            
            while ( bson_iterator_next(&sit) != BSON_EOO ) {
                WISHDEBUG(LOG_CRITICAL, "  sub object %i: %s", bson_iterator_type(&sit), bson_iterator_key(&sit));
                if (strncmp("sign", bson_iterator_key(&sit), 5) == 0 && bson_iterator_type(&sit) == BSON_BINDATA && bson_iterator_bin_len(&sit) == WISH_SIGNATURE_LEN ) {
                    signature.base = (char*) bson_iterator_bin_data(&sit);
                    signature.len = bson_iterator_bin_len(&sit);
                } else if (strncmp("uid", bson_iterator_key(&sit), 4) == 0 && bson_iterator_type(&sit) == BSON_BINDATA && bson_iterator_bin_len(&sit) == WISH_UID_LEN ) {
                    uid = bson_iterator_bin_data(&sit);
                    bson_append_element(&b, bson_iterator_key(&sit), &sit);
                } else if (strncmp("claim", bson_iterator_key(&sit), 6) == 0 && bson_iterator_type(&sit) == BSON_BINDATA ) {
                    claim.base = (char*) bson_iterator_bin_data(&sit);
                    claim.len = bson_iterator_bin_len(&sit);
                    bson_append_element(&b, bson_iterator_key(&sit), &sit);
                } else {
                    bson_append_element(&b, bson_iterator_key(&sit), &sit);
                }
            }
            
            if (signature.base != NULL && uid != NULL) {
                wish_identity_t id;
                
                if ( RET_SUCCESS == wish_identity_load(uid, &id) ) {
                    if ( RET_SUCCESS == wish_identity_verify(core, &id, &data, &claim, &signature) ) {
                        bson_append_bool(&b, "sign", true);
                    } else {
                        bson_append_bool(&b, "sign", false);
                    }
                } else {
                    bson_append_null(&b, "sign");
                }
            }
            
            bson_append_finish_object(&b);
        } while ( bson_iterator_next(&it) != BSON_EOO );
    }

    bson_append_finish_array(&b);

    bson_append_finish_object(&b);
    bson_finish(&b);

    if(b.err != 0) {
        wish_rpc_server_error(req, 344, "Failed writing reponse.");
        return;
    }

    wish_rpc_server_send(req, bson_data(&b), bson_size(&b));
}

/**
 * identity.friendRequest
 *
 * args: [{ 
    data: Buffer,
    meta?: Buffer,
    signatures?: any[] }]
 */
void wish_api_identity_friend_request(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = (wish_core_t*) req->server->context;
    
    bson_iterator it;
    bson_find_from_buffer(&it, args, "0");

    const char* luid = NULL;
    
    if (bson_iterator_type(&it) != BSON_BINDATA || bson_iterator_bin_len(&it) != WISH_UID_LEN ) {
        wish_rpc_server_error(req, 345, "Expected luid: Buffer(32)");
        return;
    }
    
    luid = bson_iterator_bin_data(&it);

    bson_find_from_buffer(&it, args, "1");
    
    if(bson_iterator_type(&it) != BSON_OBJECT) {
        wish_rpc_server_error(req, 345, "Expected object");
        return;
    }

    bson_iterator_from_buffer(&it, args);

    if ( bson_find_fieldpath_value("1.data", &it) != BSON_BINDATA ) {
        WISHDEBUG(LOG_CRITICAL, "1.data not bin data");

        wish_rpc_server_error(req, 345, "Object does not have { data: <Buffer> }.");
        return;
    }

    bson_iterator data;
    bson_iterator_from_buffer(&data, bson_iterator_bin_data(&it));
    
    const char* ruid = NULL;
    const char* pubkey = NULL;
    const char* alias = NULL;
    
    bson_find_fieldpath_value("uid", &data);
    
    if (bson_iterator_type(&data) != BSON_BINDATA || bson_iterator_bin_len(&data) != WISH_UID_LEN ) {
        wish_rpc_server_error(req, 351, "uid not Buffer(32)");
        return;
    }

    ruid = bson_iterator_bin_data(&data);
    
    bson_iterator_from_buffer(&data, bson_iterator_bin_data(&it));
    bson_find_fieldpath_value("pubkey", &data);
    
    if (bson_iterator_type(&data) != BSON_BINDATA || bson_iterator_bin_len(&data) != WISH_PUBKEY_LEN ) {
        wish_rpc_server_error(req, 351, "pubkey not Buffer(32)");
        return;
    }

    pubkey = bson_iterator_bin_data(&data);
    
    bson_iterator_from_buffer(&data, bson_iterator_bin_data(&it));
    bson_find_fieldpath_value("alias", &data);
    
    if (bson_iterator_type(&data) != BSON_STRING) {
        wish_rpc_server_error(req, 351, "alias not string");
        return;
    }

    alias = bson_iterator_string(&data);

    const char* transport = NULL;
    bson_iterator_from_buffer(&it, args);

    if ( bson_find_fieldpath_value("1.meta", &it) == BSON_BINDATA ) {
        WISHDEBUG(LOG_CRITICAL, "1.meta");
        bson_iterator meta;
        bson_iterator_from_buffer(&meta, bson_iterator_bin_data(&it));


        bson_find_fieldpath_value("transports.0", &meta);

        if (bson_iterator_type(&meta) != BSON_STRING) {
            wish_rpc_server_error(req, 351, "transports not string");
            return;
        }

        if ( memcmp("wish://", bson_iterator_string(&meta), 7) ) {
            transport = bson_iterator_string(&meta) + 7;
        } else {
            transport = bson_iterator_string(&meta);
        }
    }
    
    if (transport == NULL) {
        wish_rpc_server_error(req, 351, "No transports available.");
    }
    
    WISHDEBUG(LOG_CRITICAL, "alias for remote friend req: %s", alias);
    WISHDEBUG(LOG_CRITICAL, "tranport for remote friend req: %s", transport);


    
    wish_connection_t* friend_req_ctx = wish_connection_init(core, luid, ruid);
    friend_req_ctx->friend_req_connection = true;
    //memcpy(friend_req_ctx->rhid, rhid, WISH_ID_LEN);
        
    //uint8_t *ip = db[i].transport_ip.addr;
    
    wish_ip_addr_t ip;
    uint16_t port;
    
    wish_parse_transport_ip_port(transport, strnlen(transport, 32), &ip, &port);
    
    WISHDEBUG(LOG_CRITICAL, "Will start a friend req connection to: %u.%u.%u.%u\n", ip.addr[0], ip.addr[1], ip.addr[2], ip.addr[3]);

    wish_open_connection(core, friend_req_ctx, &ip, port, false);
    
    
    
    
    
    
    uint8_t buffer[WISH_PORT_RPC_BUFFER_SZ];

    bson b;
    bson_init_buffer(&b, buffer, WISH_PORT_RPC_BUFFER_SZ);
    bson_append_bool(&b, "data", true);
    bson_finish(&b);

    if(b.err != 0) {
        wish_rpc_server_error(req, 344, "Failed writing reponse.");
        return;
    }

    wish_rpc_server_send(req, bson_data(&b), bson_size(&b));
}

/**
 * identity.friendRequestList
 *
 * App to core: { op: "identity.friendRequestList", args: [], id: 5 }
 */
void wish_api_identity_friend_request_list(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = (wish_core_t*) req->server->context;
    
    int buffer_len = WISH_PORT_RPC_BUFFER_SZ;
    uint8_t buffer[buffer_len];

    bson bs;

    bson_init_buffer(&bs, buffer, buffer_len);
    bson_append_start_array(&bs, "data");
    
    wish_relationship_req_t* elt;
    
    int i = 0;
    
    DL_FOREACH(core->relationship_req_db, elt) {
        char idx[21];
        BSON_NUMSTR(idx, i);
        bson_append_start_object(&bs, idx);
        bson_append_binary(&bs, "luid", elt->luid, WISH_UID_LEN);
        bson_append_binary(&bs, "ruid", elt->id.uid, WISH_UID_LEN);
        bson_append_string(&bs, "alias", elt->id.alias);
        bson_append_binary(&bs, "pubkey", elt->id.pubkey, WISH_PUBKEY_LEN);
        bson_append_finish_object(&bs);
    }
    
    bson_append_finish_array(&bs);
    bson_finish(&bs);

    if(bs.err != 0) {
        wish_rpc_server_error(req, 344, "Failed writing reponse.");
        return;
    }

    wish_rpc_server_send(req, bson_data(&bs), bson_size(&bs));
}

/**
 * identity.friendRequestAccept
 *
 * App to core: { op: "identity.friendRequestAccept", args: [ <Buffer> luid, <Buffer> ruid ], id: 5 }
 */
void wish_api_identity_friend_request_accept(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = (wish_core_t*) req->server->context;


    bson_iterator it;
    bson_find_from_buffer(&it, args, "0");
    
    if(bson_iterator_type(&it) != BSON_BINDATA || bson_iterator_bin_len(&it) != WISH_ID_LEN) {
        wish_rpc_server_error(req, 345, "Invalid luid.");
        return;
    }

    const char* luid = 0;
    luid = bson_iterator_bin_data(&it);

    bson_find_from_buffer(&it, args, "1");
    
    if(bson_iterator_type(&it) != BSON_BINDATA || bson_iterator_bin_len(&it) != WISH_ID_LEN) {
        wish_rpc_server_error(req, 345, "Invalid ruid.");
        return;
    }

    const char* ruid = 0;
    ruid = bson_iterator_bin_data(&it);
    
    wish_relationship_req_t* elt;
    wish_relationship_req_t* tmp;
    
    bool found = false;
    
    DL_FOREACH_SAFE(core->relationship_req_db, elt, tmp) {
        if ( memcmp(elt->luid, luid, WISH_UID_LEN) == 0 
                && memcmp(elt->id.uid, ruid, WISH_UID_LEN) == 0 ) {
            found = true;
            DL_DELETE(core->relationship_req_db, elt);
            break;
        }
    }
    
    if (!found) {
        wish_rpc_server_error(req, 356, "No such friend request found.");
        return;
    }
    
    // Find the connection which was used for receiving the friend request   
    
    int i = 0;

    found = false;
    
    wish_connection_t* wish_connection = NULL;

    for (i = 0; i < WISH_CONTEXT_POOL_SZ; i++) {
        if (core->connection_pool[i].context_state == WISH_CONTEXT_FREE) {
            continue;
        }

        if (memcmp(core->connection_pool[i].luid, luid, WISH_ID_LEN) == 0) {
            if (memcmp(core->connection_pool[i].ruid, ruid, WISH_ID_LEN) == 0) {
                found = true;
                WISHDEBUG(LOG_CRITICAL, "Found the connection used for friend request, cnx state %i proto state: %i", core->connection_pool[i].context_state, core->connection_pool[i].curr_protocol_state);
                wish_connection = &core->connection_pool[i];
                break;
            }
            else {
                WISHDEBUG(LOG_DEBUG, "ruid mismatch");
            }
        }
        else {
            WISHDEBUG(LOG_DEBUG, "luid mismatch");
        }

    }

    if (!found) {
        wish_rpc_server_error(req, 344, "Friend request connection not found while trying to accept.");
        return;
    }

    
    // found the connection (wish_connection)

    // Check if identity is already in db

    int num_uids_in_db = wish_get_num_uid_entries();
    wish_uid_list_elem_t uid_list[num_uids_in_db];
    int num_uids = wish_load_uid_list(uid_list, num_uids_in_db);


    found = false;
    i = 0;
    for (i = 0; i < num_uids; i++) {
        if ( memcmp(&uid_list[i].uid, ruid, WISH_ID_LEN) == 0 ) {
            WISHDEBUG(LOG_CRITICAL, "Identity already in DB, we wont add it multiple times.");
            found = true;
            break;
        }
    }

    if(!found) {
        wish_save_identity_entry(&elt->id);
    }

    
    WISHDEBUG(LOG_CRITICAL, "Accepting friend request");
    
    /* The friend request has been accepted, send our certificate as a RPC response to the remote core that originally sent us the core-to-core friend request. */
    size_t signed_cert_buffer_len = 1024;
    uint8_t signed_cert_buffer[signed_cert_buffer_len];
    bin signed_cert = { .base = signed_cert_buffer, .len = signed_cert_buffer_len };
    
    if (wish_build_signed_cert(core, elt->luid, &signed_cert) == RET_FAIL) {
        WISHDEBUG(LOG_CRITICAL, "Could not construct the signed cert");
        return;
    }

    bson cert;
    bson_init_with_data(&cert, signed_cert.base);
    
    char buf_base[WISH_PORT_RPC_BUFFER_SZ];
    
    bin buf;
    buf.base = buf_base;
    buf.len = WISH_PORT_RPC_BUFFER_SZ;
    
    bson b;
    bson_init_buffer(&b, buf.base, buf.len);
    bson_append_bson(&b, "data", &cert);
    bson_finish(&b);
    
    bson_visit("Signed cert buffer: ", bson_data(&b));

    wish_rpc_server_send(&(elt->friend_rpc_req), bson_data(&b), bson_size(&b));
    
    WISHDEBUG(LOG_CRITICAL, "Send friend req reply, closing connection now");
    wish_close_connection(core, wish_connection);
            
    
    /* Send RPC reply to the App that performed the friendRequestAccept RPC*/
    int buffer_len = WISH_PORT_RPC_BUFFER_SZ;
    uint8_t buffer[buffer_len];

    bson bs;

    bson_init_buffer(&bs, buffer, buffer_len);
    bson_append_bool(&bs, "data", true);
    bson_finish(&bs);

    if(bs.err != 0) {
        wish_rpc_server_error(req, 344, "Failed writing reponse.");
        return;
    }

    wish_rpc_server_send(req, bson_data(&bs), bson_size(&bs));
    wish_platform_free(elt);
}

/**
 * identity.friendRequestDecline
 *
 * App to core: { op: "identity.friendRequestDecline", args: [ <Buffer> luid, <Buffer> ruid ], id: 5 }
 */
void wish_api_identity_friend_request_decline(rpc_server_req* req, const uint8_t* args) {
    wish_core_t* core = (wish_core_t*) req->server->context;


    bson_iterator it;
    bson_find_from_buffer(&it, args, "0");
    
    if(bson_iterator_type(&it) != BSON_BINDATA || bson_iterator_bin_len(&it) != WISH_ID_LEN) {
        wish_rpc_server_error(req, 345, "Invalid luid.");
        return;
    }

    const char* luid = 0;
    luid = bson_iterator_bin_data(&it);

    bson_find_from_buffer(&it, args, "1");
    
    if(bson_iterator_type(&it) != BSON_BINDATA || bson_iterator_bin_len(&it) != WISH_ID_LEN) {
        wish_rpc_server_error(req, 345, "Invalid ruid.");
        return;
    }

    const char* ruid = 0;
    ruid = bson_iterator_bin_data(&it);
    
    wish_relationship_req_t* elt;
    wish_relationship_req_t* tmp;
    
    bool found = false;
    
    DL_FOREACH_SAFE(core->relationship_req_db, elt, tmp) {
        if ( memcmp(elt->luid, luid, WISH_UID_LEN) == 0 
                && memcmp(elt->id.uid, ruid, WISH_UID_LEN) == 0 ) {
            found = true;
            DL_DELETE(core->relationship_req_db, elt);
            break;
        }
    }
    
    if (!found) {
        wish_rpc_server_error(req, 356, "No such friend request found.");
        return;
    }
    
    // Find the connection which was used for receiving the friend request   
    
    int i = 0;
    found = false;
    
    wish_connection_t* wish_connection = NULL;

    for (i = 0; i < WISH_CONTEXT_POOL_SZ; i++) {
        if (core->connection_pool[i].context_state == WISH_CONTEXT_FREE) {
            continue;
        }

        if (memcmp(core->connection_pool[i].luid, luid, WISH_ID_LEN) == 0) {
            if (memcmp(core->connection_pool[i].ruid, ruid, WISH_ID_LEN) == 0) {
                found = true;
                WISHDEBUG(LOG_CRITICAL, "Found the connection used for friend request, cnx state %i proto state: %i", core->connection_pool[i].context_state, core->connection_pool[i].curr_protocol_state);
                wish_connection = &core->connection_pool[i];
                break;
            }
            else {
                WISHDEBUG(LOG_DEBUG, "ruid mismatch");
            }
        }
        else {
            WISHDEBUG(LOG_DEBUG, "luid mismatch");
        }

    }

    if (!found) {
        wish_rpc_server_error(req, 344, "Friend request connection not found while trying to accept.");
        return;
    }
    
    // found the connection (wish_connection)
    
    WISHDEBUG(LOG_CRITICAL, "Declining friend request (informing requester) and closing connection");
    
    /* Send a relationship decline notification to remote core, as an RPC error */
    wish_rpc_server_error(&(elt->friend_rpc_req), 123, "Declining friend request.");
    wish_close_connection(core, wish_connection);
    
    int buffer_len = WISH_PORT_RPC_BUFFER_SZ;
    uint8_t buffer[buffer_len];

    bson bs;

    bson_init_buffer(&bs, buffer, buffer_len);
    bson_append_bool(&bs, "data", true);
    bson_finish(&bs);

    if(bs.err != 0) {
        wish_rpc_server_error(req, 344, "Failed writing reponse.");
        return;
    }

    wish_rpc_server_send(req, bson_data(&bs), bson_size(&bs));
    wish_platform_free(elt);
}

/** Report the existence of the new identity to local services:
 *
 * Let the new identity to be i.
 * Let the local host identity to be h.
 * For every service "s" present in the local service registry, do;
 *    For every service "r" present in the local service registry, do:
 *      Construct "type: peer", "online: true", message with: <luid=i, ruid=i, rsid=r, rhid=h> and send it to s. If r == s, skip to avoid sending online message to service itself.
 *    done
 * done.      
 * 
 * @param identity the identity to send updates for
 * @param online true, if the identity is online (e.g. true when identity is created, false when identity is deleted)
 */
void wish_report_identity_to_local_services(wish_core_t* core, wish_identity_t* identity, bool online) {
    uint8_t local_hostid[WISH_WHID_LEN];
    wish_core_get_host_id(core, local_hostid);
    struct wish_service_entry *service_registry = wish_service_get_registry(core);
    int i = 0;
    for (i = 0; i < WISH_MAX_SERVICES; i++) {
        if (wish_service_entry_is_valid(core, &(service_registry[i]))) {
            int j = 0;
            for (j = 0; j < WISH_MAX_SERVICES; j++) {
                if (wish_service_entry_is_valid(core, &(service_registry[j]))) {
                    if (memcmp(service_registry[i].wsid, service_registry[j].wsid, WISH_WSID_LEN) != 0) {
                        
                        bson bs;
                        int buffer_len = 2 * WISH_ID_LEN + WISH_WSID_LEN + WISH_WHID_LEN + WISH_PROTOCOL_NAME_MAX_LEN + 200;
                        uint8_t buffer[buffer_len];
                        bson_init_buffer(&bs, buffer, buffer_len);

                        bson_append_string(&bs, "type", "peer");
                        bson_append_start_object(&bs, "peer");
                        bson_append_binary(&bs, "luid", (uint8_t*) identity->uid, WISH_ID_LEN);
                        bson_append_binary(&bs, "ruid", (uint8_t*) identity->uid, WISH_ID_LEN);
                        bson_append_binary(&bs, "rsid", (uint8_t*) service_registry[j].wsid, WISH_WSID_LEN);
                        bson_append_binary(&bs, "rhid", (uint8_t*) local_hostid, WISH_ID_LEN);
                        /* FIXME support more protocols than just one */
                        bson_append_string(&bs, "protocol", service_registry[j].protocols[0].name);
                        bson_append_string(&bs, "type", "N");   /* FIXME will be type:"D" someday when deleting identity? */
                        bson_append_bool(&bs, "online", online);
                        bson_append_finish_object(&bs);

                        bson_finish(&bs);
                        
                        if (bs.err) {
                            WISHDEBUG(LOG_CRITICAL, "BSON error when creating peer message: %i %s len %i", bs.err, bs.errstr, bs.dataSize);
                        }
                        else {
                            WISHDEBUG(LOG_CRITICAL, "wish_core_app_rpc_func: Sending online message to app %s:", service_registry[i].name);
                            //bson_visit("Sending peer message to app:", buffer);
                            send_core_to_app(core, service_registry[i].wsid, (uint8_t *) bson_data(&bs), bson_size(&bs));
                        }
                    }
                }
            }
        }
    }
}