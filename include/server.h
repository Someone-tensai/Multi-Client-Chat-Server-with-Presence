#ifndef SERVER_H
#define SERVER_H

#include "common.h"
#include<sys/socket.h>

#define BACKLOG 10
// Function Declarations
void run_server(int port);
void handle_client(int client_fd);

#endif