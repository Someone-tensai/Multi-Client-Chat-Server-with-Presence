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
#include <time.h>

// ─────────────────────────────────────────────────────────────────────────────
// TLS — global SSL context (NULL when TLS is disabled)
// ─────────────────────────────────────────────────────────────────────────────
static SSL_CTX *ssl_ctx = NULL;

static SSL_CTX *tls_init(const char *cert_file, const char *key_file)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx)
    {
        fprintf(stderr, "TLS: SSL_CTX_new failed\n");
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file (ctx, key_file,  SSL_FILETYPE_PEM) <= 0)
    {
        printf("TLS: certificate files not found (%s / %s) — TLS disabled\n"
               "     To enable TLS, generate a self-signed cert:\n"
               "       openssl req -x509 -newkey rsa:2048 -keyout server.key \\\n"
               "                   -out server.crt -days 365 -nodes -subj '/CN=localhost'\n",
               cert_file, key_file);
        SSL_CTX_free(ctx);
        return NULL;
    }

    printf("TLS: loaded certificate and key\n");
    return ctx;
}

// ─────────────────────────────────────────────────────────────────────────────
// conn_send / conn_recv — transparent TLS wrappers
//
// conn_send retries on EAGAIN/partial writes so that non-blocking sockets
// never silently drop replies.  For small protocol messages (well under 4 KB)
// the loop will almost always complete in a single iteration.
// ─────────────────────────────────────────────────────────────────────────────
ssize_t conn_send(conn_t *conn, const char *buf, size_t len)
{
    if (conn->ssl)
        return (ssize_t)SSL_write(conn->ssl, buf, (int)len);

    // Non-blocking plain-text send with retry on EAGAIN / partial write
    size_t total = 0;
    while (total < len)
    {
        ssize_t n = send(conn->fd, buf + total, len - total, MSG_NOSIGNAL);
        if (n > 0)
        {
            total += (size_t)n;
        }
        else if (n == 0)
        {
            // Peer closed
            return (ssize_t)total ? (ssize_t)total : -1;
        }
        else
        {
            // n < 0
            if (errno == EINTR)
                continue;   // signal interrupted, retry
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Kernel buffer temporarily full — brief pause then retry.
                // This is acceptable because we are inside a dedicated
                // worker thread processing this connection.
                struct timespec ts = {0, 1000000L}; // 1 ms
                nanosleep(&ts, NULL);
                continue;
            }
            return -1;  // real error
        }
    }
    return (ssize_t)total;
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
static void epoll_add(int epoll_fd, conn_t *conn)
{
    struct epoll_event ev;
    ev.events   = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
    ev.data.ptr = conn;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn->fd, &ev) < 0)
        perror("epoll_ctl ADD");
}

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
// main entry — load config, then start the server
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char *argv[])
{
    server_config_t cfg;
    const char *conf_path = "server.conf";

    if (argc > 1)
        conf_path = argv[1];

    if (config_load(&cfg, conf_path) == 0)
        printf("Loaded config from %s\n", conf_path);
    else
        printf("No config file found — using defaults\n");

    run_server(&cfg);
}

// ─────────────────────────────────────────────────────────────────────────────
// run_server
// ─────────────────────────────────────────────────────────────────────────────
#define MAX_EVENTS 64

void run_server(server_config_t *cfg)
{
    // Install SIGINT handler
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    // ── TLS (optional) ───────────────────────────────────────────────────────
    ssl_ctx = tls_init(cfg->tls_cert, cfg->tls_key);
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
    addr.sin_port        = htons(cfg->port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        { perror("bind"); exit(EXIT_FAILURE); }

    if (listen(server_fd, BACKLOG) < 0)
        { perror("listen"); exit(EXIT_FAILURE); }

    // ── Thread pool (dynamic) ─────────────────────────────────────────────────
    threadpool_t *pool = threadpool_create(cfg->thread_pool_size);
    if (!pool) { perror("threadpool_create"); exit(EXIT_FAILURE); }
    pool->min_threads     = cfg->pool_min_threads;
    pool->shrink_idle_sec = cfg->pool_shrink_idle_sec;

    // ── epoll ─────────────────────────────────────────────────────────────────
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) { perror("epoll_create1"); exit(EXIT_FAILURE); }

    struct epoll_event ev;
    ev.events   = EPOLLIN;
    ev.data.fd  = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

    printf("Server listening on port %d (%s) — Ctrl+C to shut down\n",
           cfg->port, ssl_ctx ? "TLS" : "plain-text");

    struct epoll_event events[MAX_EVENTS];
    time_t last_shrink_check = time(NULL);

    while (!shutdown_flag)
    {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 500);

        if (n < 0)
        {
            if (errno == EINTR) break;
            perror("epoll_wait");
            break;
        }

        // ── Periodic dynamic pool shrink check ────────────────────────────
        time_t now = time(NULL);
        if (now - last_shrink_check >= 5)
        {
            threadpool_maybe_shrink(pool);
            last_shrink_check = now;
        }

        for (int i = 0; i < n; i++)
        {
            // ── New connection ────────────────────────────────────────────
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

                conn_t *conn = conn_create(client_fd, epoll_fd);
                if (!conn)
                {
                    fprintf(stderr, "conn_create: out of memory\n");
                    close(client_fd);
                    continue;
                }

                // Send protocol greeting BEFORE setting O_NONBLOCK so send()
                // cannot fail with EAGAIN.  Client reads this byte first to
                // decide whether to attempt TLS.
                char greeting = ssl_ctx ? 'T' : 'P';
                if (send(client_fd, &greeting, 1, MSG_NOSIGNAL) != 1)
                {
                    perror("send greeting");
                    conn_free(conn);
                    close(client_fd);
                    continue;
                }

                // Now switch to non-blocking for the epoll-driven I/O loop
                int flags = fcntl(client_fd, F_GETFL, 0);
                fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

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

                epoll_add(epoll_fd, conn);
                continue;
            }

            // ── Existing client has data (or disconnected) ────────────────
            conn_t *conn = (conn_t *)events[i].data.ptr;

            if (events[i].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP))
                conn->closing = 1;

            if (threadpool_submit(pool, conn) != 0)
            {
                fprintf(stderr, "threadpool_submit failed (queue full?)\n");
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

    struct timespec wait = {0, 200000000L};
    nanosleep(&wait, NULL);

    threadpool_destroy(pool);

    close(epoll_fd);
    close(server_fd);

    db_close();

    if (ssl_ctx)
        SSL_CTX_free(ssl_ctx);

    printf("Server shut down cleanly.\n");
}
