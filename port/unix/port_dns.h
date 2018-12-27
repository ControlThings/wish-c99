#pragma once

#include "wish_connection.h"

int port_dns_start_resolving_wish_conn(wish_connection_t *conn, char *qname);

int port_dns_start_resolving_relay_client(wish_relay_client_t *rc, char *qname);

int dns_poll_resolvers(void);

