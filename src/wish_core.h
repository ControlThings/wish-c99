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
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define WISH_CONTEXT_POOL_SZ (WISH_PORT_CONTEXT_POOL_SZ)

#define WISH_MAX_SERVICES 10 /* contrast with NUM_WISH_APPS due to be removed in wish_app.h */

#define WISH_ID_LEN     32

#ifndef WISH_UID_LEN
#define WISH_UID_LEN WISH_ID_LEN
#else
#if     WISH_UID_LEN != WISH_ID_LEN
#error  A previous definition of WISH_UID_LEN is inconsistent, != 32!
#endif
#endif

#define WISH_WHID_LEN   32
#define WISH_WSID_LEN   32
    
#define WISH_PROTOCOL_NAME_MAX_LEN 10
    
#define WISH_WLD_CLASS_MAX_LEN 48

typedef struct {
    char* base;
    int len;
} bin;
    
#include "wish_return.h"
#include "stdint.h"
#include "stdbool.h"
    
#include "wish_port_config.h"
#include "wish_rpc.h"
#include "bson.h"

typedef struct {
    uint8_t name[WISH_PROTOCOL_NAME_MAX_LEN];
} wish_protocol_t;

#define WISH_APP_NAME_MAX_LEN 32
#define WISH_APP_MAX_PROTOCOLS 2
    
typedef struct wish_service_entry {
    uint8_t wsid[WISH_WSID_LEN];
    char name[WISH_APP_NAME_MAX_LEN];
    wish_protocol_t protocols[WISH_APP_MAX_PROTOCOLS]; 
    //uint8_t permissions[WISH_PERMISSION_NAME_MAX_LEN][WISH_APP_MAX_PERMISSIONS];
} wish_app_entry_t;

typedef struct {
    uint8_t uid[WISH_ID_LEN];
} wish_uid_list_elem_t;

struct wish_core;

typedef uint32_t wish_time_t;
#define WISH_TIME_T_MAX UINT32_MAX

typedef struct wish_timer_db {
    void (*cb)(struct wish_core* core, void* ctx);
    void *cb_ctx;
    wish_time_t time;
    wish_time_t interval;
    bool singleShot;
    struct wish_timer_db* next;
} wish_timer_db_t;

typedef int wish_connection_id_t;

typedef enum wish_discovery_type {
    LocalDiscovery, RemoteFriendRequest, FriendRecommendation
} wish_discovery_type_t;

/*
typedef struct {
    wish_identity_t id;
    wish_discovery_type_t discovery_type;
    bson* meta;
} wish_relationship_t;

typedef struct wish_claim_t {
    uint8_t* signature;
    bson* document;
} wish_claim_t;
*/

struct wish_context;
struct wish_ldiscover_t;
struct wish_relationship_t;
struct wish_relay_client_ctx;
struct wish_acl;
struct wish_directory;

/**
 * Wish Core object
 */
typedef struct wish_core {
    /* Configurations */
    bool config_skip_connection_acl;
    bool config_skip_service_acl;
    
    uint8_t id[WISH_WHID_LEN];
    
    /* TCP Server */
    uint16_t wish_server_port;
    
    /* Identities */
    int num_ids;
    int loaded_num_ids;
    wish_uid_list_elem_t uid_list[WISH_PORT_MAX_UIDS];
    
    /* RPC Servers */
    #ifdef WISH_RPC_SERVER_STATIC_REQUEST_POOL
    #define REQUEST_POOL_SIZE (3*WISH_CONTEXT_POOL_SZ)
    struct wish_rpc_context_list_elem request_pool[REQUEST_POOL_SIZE];
    #endif

    rpc_server* core_api;
    rpc_server* app_api;
    rpc_server* friend_req_api;
    
    /* Services */
    struct wish_service_entry* service_registry;
    
    rpc_client* core_rpc_client;
    
    /* The number of seconds since core startup is stored here */
    wish_time_t core_time;
    wish_timer_db_t* time_db;

    /* Connections */
    struct wish_context* connection_pool;
    wish_connection_id_t next_conn_id;
    
    /* Instantiate Relay client to a server with specied IP addr and port */
    struct wish_relay_client_ctx* relay_db;

    /* Local discovery */
    bool ldiscover_allowed;
    struct wish_ldiscover_t* ldiscovery_db;
    
    /* Relationship management */
    struct wish_relationship_req_t* relationship_req_db;
    struct wish_relationship_t* relationship_db;
    
    /* Access control */
    struct wish_acl* acl;
    
    /* Wish Directory */
    struct wish_directory* directory;
    
    /** Storage for an optional local discovery class string, which will be announced as meta.product in the broadcast */
    char wld_class[WISH_WLD_CLASS_MAX_LEN];
} wish_core_t;

#include "wish_config.h"

int wish_core_update_identities(wish_core_t* core);

#ifdef __cplusplus
}
#endif
