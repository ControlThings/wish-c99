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

/**
 * @file wish_api_wld.h
 * @brief Core-app RPC API handlers for 'wld.*' requests.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"
#include "uthash.h"
    
#include "wish_core.h"
    
    /* wld API */
    
    void wish_api_wld_list(rpc_server_req* req, const uint8_t* args);
    
    void wish_api_wld_announce(rpc_server_req* req, const uint8_t* args);

    void wish_api_wld_clear(rpc_server_req* req, const uint8_t* args);

    void wish_api_wld_friend_request(rpc_server_req* req, const uint8_t* args);
    
#ifdef __cplusplus
}
#endif
