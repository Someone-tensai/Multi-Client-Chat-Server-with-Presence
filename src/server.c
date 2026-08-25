#include "../include/server.h"
#include "../include/registry.h"
#include <pthread.h>

int main()
{
    run_server(DEFAULT_PORT);
}

// Thread entry point — each client gets its own thread
void *client_thread(void *arg)
{
    int client_fd = *(int *)arg;
    free(arg);
    handle_client(client_fd);
    return NULL;
}

void run_server(int port)
{
    int server_fd;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0)
    {
        perror("Server Socket Creation Failed");
        exit(EXIT_FAILURE);
    }

    // Allow port reuse so restart doesn't fail with "Address already in use"
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Defining Server Socket Address
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    int status = bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    if(status < 0)
    {
        perror("Server Socket Binding Failed");
        exit(EXIT_FAILURE);
    }

    // Start Listening for Connections
    int stat = listen(server_fd, BACKLOG);
    if(stat < 0)
    {
        perror("Server Listen Failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", port);

    struct sockaddr_in client_address;
    socklen_t size_address = sizeof(client_address);

    while(1)
    {
        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr*)&client_address, &size_address);
        if(*client_fd == -1)
        {
            perror("Error Connecting to Client");
            free(client_fd);
            continue;
        }

        printf("New client connected (fd=%d)\n", *client_fd);

        // Spawn a thread for each client
        pthread_t tid;
        if(pthread_create(&tid, NULL, client_thread, client_fd) != 0)
        {
            perror("Thread creation failed");
            close(*client_fd);
            free(client_fd);
            continue;
        }
        // Detach so thread cleans itself up when done
        pthread_detach(tid);
    }
}

