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
/*
 * Wish C99 Linux-specific networking helper code
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include<stdio.h> //printf
#include<string.h>    //memset
#include<errno.h> //errno
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include "helper.h"
#else
#include<sys/socket.h>
#include<netdb.h>
#include<ifaddrs.h>
#endif

#ifdef __APPLE__
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <stdio.h>
#endif

#include<stdlib.h>
#include<unistd.h>
 
#include "wish_connection.h"
#include "wish_connection_mgr.h"

/*
 * Find local ip used as source ip in ip packets.
 * Read the /proc/net/route file
 */
static void find_local_ip_default_route(char *addr_buffer, 
        size_t addr_buffer_len) {
#if defined(__APPLE__)
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in *sa;
    char *addr;
    
    getifaddrs (&ifap);
    int c = 0;
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr->sa_family==AF_INET) {
            if(c==0) { c++; continue; }
            sa = (struct sockaddr_in *) ifa->ifa_addr;
            addr = inet_ntoa(sa->sin_addr);
            //printf("Interface: %s\tAddress: %s\n", ifa->ifa_name, addr);
            strncpy(addr_buffer, addr, addr_buffer_len);
            
            freeifaddrs(ifap);
            return;
            break;
        }
    }
#elif defined(_WIN32)
    
#warning find_local_ip_default_route is unimplemented for the win32 platform.

#else
    FILE *f;
    char line[100] , *p=NULL , *c;
     
    f = fopen("/proc/net/route" , "r");
     
    while(fgets(line , 100 , f))
    {
        p = strtok(line , " \t");
        c = strtok(NULL , " \t");
         
        if(p!=NULL && c!=NULL)
        {
            if(strcmp(c , "00000000") == 0)
            {
                //printf("Default interface is : %s \n" , p);
                break;
            }
        }
    }
     
    //which family do we require , AF_INET or AF_INET6
    int fm = AF_INET;
    struct ifaddrs *ifaddr, *ifa;
    int family , s;
    static char host[NI_MAXHOST];
 
    if (getifaddrs(&ifaddr) == -1) 
    {
        perror("getifaddrs");
        abort();
    }
 
    //Walk through linked list, maintaining head pointer so we can free list later
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) 
    {
        if (ifa->ifa_addr == NULL)
        {
            continue;
        }
 
        family = ifa->ifa_addr->sa_family;
 
        if(strcmp( ifa->ifa_name , p) == 0)
        {
            if (family == fm) 
            {
                s = getnameinfo( ifa->ifa_addr, (family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6) , host , NI_MAXHOST , NULL , 0 , NI_NUMERICHOST);
                 
                if (s != 0) 
                {
                    printf("getnameinfo() failed: %s\n", gai_strerror(s));
                    abort();
                }
                 
                ////printf("address: %s", host);
                break;
            }
            //printf("\n");
        }
    }
 
    freeifaddrs(ifaddr);
    strncpy(addr_buffer, host, addr_buffer_len);
    fclose(f);
#endif
}

/**
 * Get the local host IP addr formatted as a C string. The retuned
 * address should be the one which is the subnet having the host's
 * default route
 *
 * @param addr_str the pointer where the address should be stored
 * @param addr_str_len the maximum allowed length of the address
 * @return Returns value 0 if all went well.
 */
int wish_get_host_ip_str(wish_core_t* core, char* addr_str, size_t addr_str_len) {
    memset(addr_str, 0, addr_str_len);
    
    find_local_ip_default_route(addr_str, addr_str_len);
    return 0;
}

void wish_set_host_port(wish_core_t* core, uint16_t port) {
    core->wish_server_port = port;
}
/** Get the local TCP port where the Wish core accepts incoming connections 
 * @return the local TCP server port
 */
int wish_get_host_port(wish_core_t* core) {
    return core->wish_server_port;

}

