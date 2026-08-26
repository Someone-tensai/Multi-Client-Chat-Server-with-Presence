#ifndef SERVER_H
#define SERVER_H

#include "common.h"
#include "conn.h"
#include "config.h"
#include <sys/socket.h>

#define BACKLOG 10

// Start the server with the given configuration.
void run_server(server_config_t *cfg);

void handle_client(conn_t *conn);
void conn_free(conn_t *conn);
ssize_t conn_send(conn_t *conn, const char *buf, size_t len);
ssize_t conn_recv(conn_t *conn, char *buf, size_t len);

#endif
