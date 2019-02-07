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
#include <string.h>
#include <stdlib.h>

#include "wish_utils.h"
#include "wish_debug.h"

return_t wish_parse_transport_ip_port(const char *url, size_t url_len, wish_ip_addr_t *ip, uint16_t *port) {
    return_t ret = RET_FAIL;
    ret = wish_parse_transport_ip(url, url_len, ip);
    
    if (ret != RET_SUCCESS) { 
        /* An IP address could not be parsed from the URL. */
        
        return ret;
    }
    
    ret = wish_parse_transport_port(url, url_len, port);

    return ret;
}

return_t wish_parse_transport_host_port(const char *url, size_t url_len, char *host, uint16_t *port) {
    return_t ret = RET_FAIL;
    
    char *colon_ptr = strrchr(url, ':');
    if (colon_ptr == NULL) {
        return RET_FAIL;
    }
    
    //Remove "wish://" from the beginning, if it exists
    char *actual_hostname = strrchr(url, '/');
    if (actual_hostname == NULL) {
        actual_hostname = (char*) url;
    }
    else {
        actual_hostname += 1; //advance past the last '/' of url
    }
    
    /* The length of the 'host' part */
    size_t hostlen = colon_ptr - actual_hostname;
    strncpy(host, actual_hostname, hostlen);
    host[hostlen] = '\0';
    
    
    ret = wish_parse_transport_port(url, url_len, port);
    
    return ret;
}

return_t wish_parse_transport_port(const char *url, size_t url_len, uint16_t *port) {
    /* FIXME implement parsing of ip address */
    int retval = RET_FAIL;
    if (port == NULL) {
        return retval;
    }

    /* Parse port number by finding the first ':' character when
     * starting from the end of the string */
    char *colon_ptr = strrchr(url, ':');
    if (colon_ptr == NULL) {
        /* colon not found. */
        return retval;
    }
    
    uint16_t parsed_port = atoi(colon_ptr+sizeof (char));
    /* XXX assumption: TCP port number cannot be 0 */
    if (parsed_port != 0) {
        *port = parsed_port;
        retval = RET_SUCCESS;
    }
    return retval;
}

/** FIXME this implementation is rather shaky. Consider using http://www.cs.cmu.edu/afs/cs/academic/class/15213-f00/unpv12e/libfree/inet_pton.c */
return_t wish_parse_transport_ip(const char *url, size_t url_len, wish_ip_addr_t *ip) {
    int retval = RET_FAIL;
    const int ip_str_max_len = 4*3+3; /* t.ex. 255.255.255.255 */
    const int ip_str_min_len = 4+3;   /* t.ex. 1.1.1.1 */
    char* first_slash = strchr(url, '/');
    const char* start_of_ip_str;
    if (first_slash == NULL) {
        // maybe only ip:port ?
        start_of_ip_str = url;
    } else {
        start_of_ip_str = first_slash+2;
    }
    char* colon = strchr(start_of_ip_str, ':');
    if (colon == NULL) {
        /* There's no colon, set colon to be end of string */
        colon = (char*) url+strlen(url) + 1;
    }
    
    int actual_ip_str_len = colon-start_of_ip_str;
    if (actual_ip_str_len > ip_str_max_len) {
        //WISHDEBUG(LOG_CRITICAL, "Parse error, IP part seems too long");
        return retval;
    }
    if (actual_ip_str_len < ip_str_min_len) {
        WISHDEBUG(LOG_CRITICAL, "Parse error, IP part seems too short");
        return retval;
    }

    if (ip == NULL) {
        return retval;
    }

    /* We now have a valid looking IP address in start_of_ip_str, of
     * length actual_ip_str_len */

    /* Parse out the bytes */
    const int num_bytes = 4; /* There are always 3 dots */
    const char *curr_byte_str = start_of_ip_str;
    int i = 0;
    for (i = 0; i < num_bytes; i++) {
        //WISHDEBUG(LOG_CRITICAL, "curr_byte_str: %s", curr_byte_str);
        ip->addr[i] = atoi(curr_byte_str);
        curr_byte_str = strchr(curr_byte_str, '.'); //Find the next '.' in the ip addr
        if (curr_byte_str == NULL && i != 3) { // Note: when i == 3, it is expect that the next dot is not found.
            return retval;
        }
        curr_byte_str += 1; //advance to the start of next byte, past the '.'
    }
    /* Note that curr_byte_str is invalid after this */
    
    retval = RET_SUCCESS;
    return retval;
}



