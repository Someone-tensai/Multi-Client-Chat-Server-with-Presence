#include "../include/server.h"
#include "../include/registry.h"
#include "../include/threadpool.h"
#include "../include/protocol.h"
#include "../include/db.h"
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <errno.h>

// ─────────────────────────────────────────────────────────────────────────────
// TLS — global SSL context (NULL when TLS is disabled)
// ─────────────────────────────────────────────────────────────────────────────
#define TLS_CERT_FILE "server.crt"
#define TLS_KEY_FILE  "server.key"

static SSL_CTX *ssl_ctx = NULL;

static SSL_CTX *tls_init(void)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx)
    {
        fprintf(stderr, "TLS: SSL_CTX_new failed\n");
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    if (SSL_CTX_use_certificate_file(ctx, TLS_CERT_FILE, SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file (ctx, TLS_KEY_FILE,  SSL_FILETYPE_PEM) <= 0)
    {
        fprintf(stderr,
            "TLS: could not load %s / %s\n"
            "     To generate a self-signed cert run:\n"
            "       openssl req -x509 -newkey rsa:2048 -keyout server.key \\\n"
            "                   -out server.crt -days 365 -nodes -subj '/CN=localhost'\n",
            TLS_CERT_FILE, TLS_KEY_FILE);
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    printf("TLS: loaded certificate and key\n");
    return ctx;
}

// ─────────────────────────────────────────────────────────────────────────────
// conn_send / conn_recv — transparent TLS wrappers
// Used by client_handler.c and registry.c (broadcast) instead of send/recv.
// ─────────────────────────────────────────────────────────────────────────────
ssize_t conn_send(conn_t *conn, const char *buf, size_t len)
{
    if (conn->ssl)
        return (ssize_t)SSL_write(conn->ssl, buf, (int)len);
    return send(conn->fd, buf, len, 0);
}

ssize_t conn_recv(conn_t *conn, char *buf, size_t len)
{
    if (conn->ssl)
        return (ssize_t)SSL_read(conn->ssl, buf, (int)len);
    return recv(conn->fd, buf, len, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// conn_t helpers
// ─────────────────────────────────────────────────────────────────────────────
static conn_t *conn_create(int fd, int epoll_fd)
{
    conn_t *conn = calloc(1, sizeof(conn_t));
    if (!conn) return NULL;
    conn->fd       = fd;
    conn->epoll_fd = epoll_fd;
    conn->ssl      = NULL;
    conn->me       = NULL;
    conn->buf_len  = 0;
    conn->closing  = 0;
    pthread_mutex_init(&conn->lock, NULL);
    return conn;
}

void conn_free(conn_t *conn)
{
    if (!conn) return;
    if (conn->ssl)
    {
        SSL_shutdown(conn->ssl);
        SSL_free(conn->ssl);
    }
    pthread_mutex_destroy(&conn->lock);
    free(conn);
}

// ─────────────────────────────────────────────────────────────────────────────
// epoll helpers
// ─────────────────────────────────────────────────────────────────────────────

// Add a new client fd to the epoll instance.
// EPOLLONESHOT: once the event fires the fd is disabled until explicitly
// re-armed by handle_client.  This guarantees only one worker processes
// a given connection at a time.
static void epoll_add(int epoll_fd, conn_t *conn)
{
    struct epoll_event ev;
    ev.events   = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
    ev.data.ptr = conn;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn->fd, &ev) < 0)
        perror("epoll_ctl ADD");
}

// Re-arm after handle_client finishes processing a burst.
void epoll_rearm(conn_t *conn)
{
    struct epoll_event ev;
    ev.events   = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
    ev.data.ptr = conn;
    epoll_ctl(conn->epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
}

// ─────────────────────────────────────────────────────────────────────────────
// SIGINT
// ─────────────────────────────────────────────────────────────────────────────
static volatile sig_atomic_t shutdown_flag = 0;

static void handle_sigint(int sig)
{
    (void)sig;
    shutdown_flag = 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// main entry
// ─────────────────────────────────────────────────────────────────────────────
int main(void)
{
    run_server(DEFAULT_PORT);
}

// ─────────────────────────────────────────────────────────────────────────────
// run_server
// ─────────────────────────────────────────────────────────────────────────────
#define MAX_EVENTS 64

void run_server(int port)
{
    // Install SIGINT handler
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    // ── TLS (optional) ───────────────────────────────────────────────────────
    ssl_ctx = tls_init();
    if (!ssl_ctx)
        printf("TLS disabled — running in plain-text mode\n"
               "(generate server.crt + server.key to enable)\n");

    // ── SQLite database ───────────────────────────────────────────────────────
    if (db_open() != 0)
    {
        fprintf(stderr, "Failed to open database — aborting\n");
        exit(EXIT_FAILURE);
    }

    // ── Server socket ─────────────────────────────────────────────────────────
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        { perror("bind"); exit(EXIT_FAILURE); }

    if (listen(server_fd, BACKLOG) < 0)
        { perror("listen"); exit(EXIT_FAILURE); }

    // ── Thread pool ───────────────────────────────────────────────────────────
    threadpool_t *pool = threadpool_create(THREADPOOL_SIZE);
    if (!pool) { perror("threadpool_create"); exit(EXIT_FAILURE); }

    // ── epoll ─────────────────────────────────────────────────────────────────
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) { perror("epoll_create1"); exit(EXIT_FAILURE); }

    // Watch the server socket for new connections (no ONESHOT — always active)
    struct epoll_event ev;
    ev.events   = EPOLLIN;
    ev.data.fd  = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

    printf("Server listening on port %d (%s) — Ctrl+C to shut down\n",
           port, ssl_ctx ? "TLS" : "plain-text");

    struct epoll_event events[MAX_EVENTS];

    while (!shutdown_flag)
    {
        // 500 ms timeout so we can check shutdown_flag regularly
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 500);

        if (n < 0)
        {
            if (errno == EINTR) break;   // SIGINT fired
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; i++)
        {
            // ── New connection ────────────────────────────────────────────────
            if (events[i].data.fd == server_fd)
            {
                struct sockaddr_in cli_addr;
                socklen_t cli_len = sizeof(cli_addr);
                int client_fd = accept(server_fd,
                                       (struct sockaddr *)&cli_addr, &cli_len);
                if (client_fd < 0)
                {
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
                        perror("accept");
                    continue;
                }

                // Make the client fd non-blocking
                int flags = fcntl(client_fd, F_GETFL, 0);
                fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

                // Allocate per-connection state
                conn_t *conn = conn_create(client_fd, epoll_fd);
                if (!conn)
                {
                    fprintf(stderr, "conn_create: out of memory\n");
                    close(client_fd);
                    continue;
                }

                // TLS handshake (if TLS is enabled)
                if (ssl_ctx)
                {
                    conn->ssl = SSL_new(ssl_ctx);
                    SSL_set_fd(conn->ssl, client_fd);
                    if (SSL_accept(conn->ssl) <= 0)
                    {
                        ERR_print_errors_fp(stderr);
                        conn_free(conn);
                        close(client_fd);
                        continue;
                    }
                }

                printf("New client connected (fd=%d%s)\n",
                       client_fd, ssl_ctx ? ", TLS" : "");

                // Register with epoll — EPOLLONESHOT arms it for the first read
                epoll_add(epoll_fd, conn);
                continue;
            }

            // ── Existing client has data (or disconnected) ────────────────────
            conn_t *conn = (conn_t *)events[i].data.ptr;

            // EPOLLRDHUP/EPOLLERR/EPOLLHUP → peer closed or error.
            // Still dispatch to the pool so handle_client can run cleanup.
            if (events[i].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP))
                conn->closing = 1;

            if (threadpool_submit(pool, conn) != 0)
            {
                fprintf(stderr, "threadpool_submit failed (queue full?)\n");
                // Re-arm so we don't lose the fd permanently
                if (!conn->closing)
                    epoll_rearm(conn);
            }
        }
    }

    // ── Graceful shutdown ─────────────────────────────────────────────────────
    printf("\nShutting down — notifying clients...\n");

    char msg[MAX_LINE_LEN];
    snprintf(msg, sizeof(msg), "ERR %s\n", ERR_SERVER_SHUTDOWN);
    notify_all_clients(msg);

    struct timespec wait = {0, 200000000L};   // 200 ms
    nanosleep(&wait, NULL);

    threadpool_destroy(pool);

    close(epoll_fd);
    close(server_fd);

    db_close();

    if (ssl_ctx)
        SSL_CTX_free(ssl_ctx);

    printf("Server shut down cleanly.\n");
}
