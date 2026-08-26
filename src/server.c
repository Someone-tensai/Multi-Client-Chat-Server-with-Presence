#include "../include/server.h"
#include "../include/registry.h"
#include "../include/threadpool.h"
#include "../include/protocol.h"
#include "../include/db.h"
#include <pthread.h>
#include <signal.h>

int main()
{
    run_server(DEFAULT_PORT);
}

// ─────────────────────────────────────────────────────────────────────────────
// SIGINT handler — sets flag only (no heavy work inside signal handlers)
// accept() will return -1 with errno == EINTR, letting the loop check the flag
// ─────────────────────────────────────────────────────────────────────────────
static volatile sig_atomic_t shutdown_flag = 0;

static void handle_sigint(int sig)
{
    (void)sig;
    shutdown_flag = 1;
}

void run_server(int port)
{
    // Install SIGINT handler without SA_RESTART so accept() is interrupted
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   // no SA_RESTART — accept() returns EINTR on Ctrl+C
    sigaction(SIGINT, &sa, NULL);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0)
    {
        perror("Server Socket Creation Failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port        = htons(port);

    if(bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0)
    {
        perror("Server Socket Binding Failed");
        exit(EXIT_FAILURE);
    }

    if(listen(server_fd, BACKLOG) < 0)
    {
        perror("Server Listen Failed");
        exit(EXIT_FAILURE);
    }

    threadpool_t *pool = threadpool_create(THREADPOOL_SIZE);
    if(!pool)
    {
        perror("Thread pool creation failed");
        exit(EXIT_FAILURE);
    }

    // Open (or create) the SQLite database
    if(db_open() != 0)
    {
        fprintf(stderr, "Failed to open database — aborting\n");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d (Ctrl+C to shut down)\n", port);

    struct sockaddr_in client_address;
    socklen_t size_address = sizeof(client_address);

    while(!shutdown_flag)
    {
        int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &size_address);

        if(client_fd == -1)
        {
            // EINTR means the signal fired and interrupted accept — check flag
            if(shutdown_flag) break;
            perror("Error Connecting to Client");
            continue;
        }

        printf("New client connected (fd=%d)\n", client_fd);

        if(threadpool_submit(pool, client_fd) != 0)
        {
            perror("Thread pool submit failed");
            close(client_fd);
        }
    }

    // ── Graceful shutdown ────────────────────────────────────────────────────
    printf("\nShutting down — notifying clients...\n");

    // Tell every connected client the server is going down
    char msg[MAX_LINE_LEN];
    snprintf(msg, sizeof(msg), "ERR %s\n", ERR_SERVER_SHUTDOWN);
    notify_all_clients(msg);

    // Give clients a moment to receive the message before we close sockets
    struct timespec wait = {0, 200000000L};   // 200 ms
    nanosleep(&wait, NULL);

    // Stop accepting work and wait for active handlers to finish
    threadpool_destroy(pool);

    db_close();
    close(server_fd);
    printf("Server shut down cleanly.\n");
}
