#ifndef COMMON_H
#define COMMON_H

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<errno.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>

// Default Port to Connect To
#define DEFAULT_PORT 8888

// Default Host to connect to (Local Host/Same client by default)
// Overridden with command line arguments
#define DEFAULT_HOST "127.0.0.1"

#define READ_BUFFER_SIZE 1024

#endif