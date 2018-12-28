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

#include <stdint.h>
#include <stddef.h>
#include "wish_ip_addr.h"
#include "wish_return.h"

/**
 * Parse out an IP address, and port from a wish transport url of the form wish://10.1.1.231:4444 or just 1.1.1.1:1
 * 
 * @param url
 * @param url_len
 * @param ip
 * @param port
 * @return RET_SUCESS if both IP and port were parsed
 */
return_t wish_parse_transport_ip_port(const char *url, size_t url_len, wish_ip_addr_t *ip, uint16_t *port);

/**
 * 
 * Parse out hostname and port from a wish transport url of the form: wish://wish.cto.fi:40000
 * 
 * @param url
 * @param url_len
 * @param host
 * @param port
 * @return RET_SUCESS if both hostname and port were parsed
 */
return_t wish_parse_transport_host_port(const char *url, size_t url_len, char *host, uint16_t *port);

/**
 * Parse a TCP port from "wish URL" such as the ones typically found in 
 * transports.
 *
 * @return RET_SUCCESS, if parsing was succesful and yeilded results. 1 for
 * errors, then the parameter port is not valid.
 */
return_t wish_parse_transport_port(const char *url, size_t url_len, uint16_t *port);

/**
 * Parse a IP address from a "wish URL"
 * @return RET_SUCCESS if the parsing was successful and yeilded results. Any
 * other return value signifies failure.
 */
return_t wish_parse_transport_ip(const char *url, size_t url_len, wish_ip_addr_t *ip);


