#pragma once

#include "wish_connection.h"

int port_dns_start_resolving_wish_conn(wish_connection_t *conn, char *qname);

int port_dns_start_resolving_relay_client(wish_relay_client_t *rc, char *qname);

int port_dns_poll_resolvers(void);

void port_dns_resolver_cancel_by_wish_connection(wish_connection_t *conn);

void port_dns_resolver_cancel_by_relay_client(wish_relay_client_t *rc);
