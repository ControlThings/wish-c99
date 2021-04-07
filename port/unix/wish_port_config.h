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

/** Port-specific config variables */

/** This specifies the size of the receive ring buffer */
#define WISH_PORT_RX_RB_SZ ( 32*1024 )

/** This specifies the maximum number of simultaneous Wish connections.
 * 
 * Unix port uses select(2), which has limitation FD_SETSIZE = 1024, and all filedescriptors must also have value less than this.
 * So number of wish connections, and app connections, and open files at any moment must be less than 1024.
 * */
#define WISH_PORT_CONTEXT_POOL_SZ  1000

/** This specifies the maximum number of simultaneous app requests to core */
#define WISH_PORT_APP_RPC_POOL_SZ ( 60 )

/** This specifies the maximum size of the buffer where some RPC handlers build the reply */
#define WISH_PORT_RPC_BUFFER_SZ ( 512*1024 )

/** This defines the maximum number of entries in the Wish local discovery table */
#define WISH_LOCAL_DISCOVERY_MAX ( 64 )

/** This defines the maximum number of uids in database (max number of identities + contacts) */
#define WISH_PORT_MAX_UIDS ( 2048 )

/** If this is defined, include support for the App TCP server */
#define WITH_APP_TCP_SERVER
//#define WITH_APP_INTERNAL

/** With this define you get fancy ANSI colored output on the console */
#define WISH_CONSOLE_COLORS

#define WISH_PORT_WITH_ASSERT

#define WISH_MAX_SERVICES 100
