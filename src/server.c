#include "../include/server.h"
#include "../include/registry.h"

int main()
{
    run_server(DEFAULT_PORT);
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
    struct sockaddr_in client_address;
    // Infinite Accept Loop
    socklen_t size_address = sizeof(client_address);
    while(1)
    {
        int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &size_address);
        if(client_fd == -1)
        {
            perror("Error Connecting to Client");
            continue;
        }
        handle_client(client_fd);
    }

}

