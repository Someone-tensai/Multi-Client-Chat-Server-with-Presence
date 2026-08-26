#include "../include/server.h"
#include "../include/registry.h"
#include "../include/threadpool.h"
#include "../include/protocol.h"
#include "../include/db.h"
#include "../include/log.h"
#include "../include/session.h"
#include "../include/receipt.h"
#include "../include/presence.h"
#include "../include/block.h"
#include "../include/permission.h"
#include "../include/invite.h"
#include "../include/redis.h"
#include "../include/pg.h"
#include "../include/metrics.h"
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
        LOG_ERROR("TLS: SSL_CTX_new failed");
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file (ctx, key_file,  SSL_FILETYPE_PEM) <= 0)
    {
        LOG_WARN("TLS: certificate files not found (%s / %s) — TLS disabled", cert_file, key_file);
        LOG_INFO("To enable TLS, generate a self-signed cert: openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt -days 365 -nodes -subj '/CN=localhost'");
        SSL_CTX_free(ctx);
        return NULL;
    }

    // Phase 17-18: TLS hardening — minimum TLS 1.2, strong ciphers
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_cipher_list(ctx, "ECDHE+AESGCM:ECDHE+CHACHA20:DHE+AESGCM:DHE+CHACHA20:!aNULL:!MD5:!DSS");
    SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION);

    LOG_INFO("TLS: loaded certificate and key (TLS >= 1.2, hardened ciphers)");
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
        LOG_ERROR_ERRNO("epoll_ctl ADD");
}

void epoll_rearm(conn_t *conn)
{
    struct epoll_event ev;
    ev.events   = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
    ev.data.ptr = conn;
    epoll_ctl(conn->epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
}

// ─────────────────────────────────────────────────────────────────────────────
// SIGINT / SIGTERM — graceful shutdown
// ─────────────────────────────────────────────────────────────────────────────
static volatile sig_atomic_t shutdown_flag = 0;

static void handle_shutdown_signal(int sig)
{
    (void)sig;
    shutdown_flag = 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Login rate limiter (Phase 17-18) — per-IP brute-force protection
// ─────────────────────────────────────────────────────────────────────────────
#define LOGIN_RATE_WINDOW   60   // seconds
#define LOGIN_RATE_MAX      10   // max attempts per window
#define LOGIN_LOCKOUT_SEC  300   // 5 minute lockout

typedef struct {
    char ip[46];
    int  attempts;
    time_t window_start;
    time_t lockout_until;
} login_rate_entry_t;

#define MAX_RATE_ENTRIES 256
static login_rate_entry_t login_rates[MAX_RATE_ENTRIES];
static int login_rate_count = 0;
static pthread_mutex_t login_rate_lock = PTHREAD_MUTEX_INITIALIZER;

static login_rate_entry_t *find_login_rate(const char *ip)
{
    for (int i = 0; i < login_rate_count; i++) {
        if (strcmp(login_rates[i].ip, ip) == 0)
            return &login_rates[i];
    }
    if (login_rate_count < MAX_RATE_ENTRIES) {
        login_rate_entry_t *e = &login_rates[login_rate_count++];
        strncpy(e->ip, ip, sizeof(e->ip) - 1);
        e->ip[sizeof(e->ip) - 1] = '\0';
        e->attempts = 0;
        e->window_start = 0;
        e->lockout_until = 0;
        return e;
    }
    return NULL;
}

int server_check_login_rate(const char *ip)
{
    if (!ip) return 0;
    time_t now = time(NULL);
    pthread_mutex_lock(&login_rate_lock);
    login_rate_entry_t *e = find_login_rate(ip);
    if (!e) { pthread_mutex_unlock(&login_rate_lock); return 0; }

    if (e->lockout_until > 0 && now < e->lockout_until) {
        pthread_mutex_unlock(&login_rate_lock);
        return -1; // locked out
    }
    if (e->lockout_until > 0 && now >= e->lockout_until) {
        e->attempts = 0;
        e->lockout_until = 0;
        e->window_start = now;
    }
    if ((now - e->window_start) > LOGIN_RATE_WINDOW) {
        e->attempts = 0;
        e->window_start = now;
    }
    e->attempts++;
    if (e->attempts > LOGIN_RATE_MAX) {
        e->lockout_until = now + LOGIN_LOCKOUT_SEC;
        pthread_mutex_unlock(&login_rate_lock);
        return -1; // lockout
    }
    pthread_mutex_unlock(&login_rate_lock);
    return 0;
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

    // Initialize logging early so subsequent startup logs are captured
    log_init(cfg.log_level, cfg.log_file);
    LOG_INFO("Loaded config from %s (log_level=%s, log_file=%s)", conf_path, cfg.log_level, cfg.log_file[0] ? cfg.log_file : "(console)");

    run_server(&cfg);
    log_close();
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// run_server
// ─────────────────────────────────────────────────────────────────────────────
#define MAX_EVENTS 64

void run_server(server_config_t *cfg)
{
    // Install SIGINT + SIGTERM handlers (Phase 34: graceful shutdown)
    struct sigaction sa;
    sa.sa_handler = handle_shutdown_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // ── TLS (optional) ───────────────────────────────────────────────────────
    ssl_ctx = tls_init(cfg->tls_cert, cfg->tls_key);
    if (!ssl_ctx)
        LOG_INFO("TLS disabled — running in plain-text mode (generate server.crt + server.key to enable)");

    // ── Registry (dynamic allocation based on config) ────────────────────────
    if (registry_init(cfg) != 0)
    {
        LOG_ERROR("Failed to initialize registry — aborting");
        exit(EXIT_FAILURE);
    }
    LOG_INFO("Registry initialized: rooms=%d clients=%d members=%d history=%d",
             cfg->max_rooms, cfg->max_clients, cfg->max_members, cfg->history_size);

    // ── SQLite database ───────────────────────────────────────────────────────
    if (db_open() != 0)
    {
        LOG_ERROR("Failed to open database — aborting");
        exit(EXIT_FAILURE);
    }

    // ── Read receipts (Phase 6) ──────────────────────────────────────────────
    if (receipt_init() != 0)
    {
        LOG_WARN("Failed to initialize read receipts — receipts disabled");
    }

    // ── Presence subsystem (Phase 7) ─────────────────────────────────────────
    if (presence_init() != 0)
    {
        LOG_WARN("Failed to initialize presence — typing indicators disabled");
    }

    // ── Block/Mute/Ban (Phase 8) ─────────────────────────────────────────────
    if (block_init() != 0)
    {
        LOG_WARN("Failed to initialize block subsystem — blocking disabled");
    }

    // ── Permissions (Phase 9) ────────────────────────────────────────────────
    if (permission_init() != 0)
    {
        LOG_WARN("Failed to initialize permissions — role system disabled");
    }

    // ── Invites (Phase 11) ───────────────────────────────────────────────────
    if (invite_init() != 0)
    {
        LOG_WARN("Failed to initialize invites — invitation system disabled");
    }

    // ── Metrics (Phase 25-26) ────────────────────────────────────────────────
    metrics_init();
    LOG_INFO("Metrics subsystem initialized");

    // ── Redis (Phase 19-22, optional) ────────────────────────────────────────
    redis_config_t redis_cfg = { .host = "127.0.0.1", .port = 6379, .enabled = 0 };
    redis_init(&redis_cfg);

    // ── PostgreSQL (Phase 23-24, optional) ────────────────────────────────────
    pg_config_t pg_cfg = { .dsn = "", .enabled = 0 };
    pg_init(&pg_cfg);

    // ── Server socket ─────────────────────────────────────────────────────────
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { LOG_ERROR_ERRNO("socket"); exit(EXIT_FAILURE); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(cfg->port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        { LOG_ERROR_ERRNO("bind"); exit(EXIT_FAILURE); }

    if (listen(server_fd, BACKLOG) < 0)
        { LOG_ERROR_ERRNO("listen"); exit(EXIT_FAILURE); }

    // ── Thread pool (dynamic) ─────────────────────────────────────────────────
    threadpool_t *pool = threadpool_create(cfg->thread_pool_size);
    if (!pool) { LOG_ERROR_ERRNO("threadpool_create"); exit(EXIT_FAILURE); }
    pool->min_threads     = cfg->pool_min_threads;
    pool->shrink_idle_sec = cfg->pool_shrink_idle_sec;

    // ── epoll ─────────────────────────────────────────────────────────────────
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) { LOG_ERROR_ERRNO("epoll_create1"); exit(EXIT_FAILURE); }

    struct epoll_event ev;
    ev.events   = EPOLLIN;
    ev.data.fd  = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

    LOG_INFO("Server listening on port %d (%s) — Ctrl+C to shut down",
           cfg->port, ssl_ctx ? "TLS" : "plain-text");

    struct epoll_event events[MAX_EVENTS];
    time_t last_shrink_check = time(NULL);
    time_t last_presence_cleanup = time(NULL);

    while (!shutdown_flag)
    {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 500);

        if (n < 0)
        {
            if (errno == EINTR) break;
            LOG_ERROR_ERRNO("epoll_wait");
            break;
        }

        // ── Periodic dynamic pool shrink check ────────────────────────────
        time_t now = time(NULL);
        if (now - last_shrink_check >= 5)
        {
            threadpool_maybe_shrink(pool);
            last_shrink_check = now;
        }

        // ── Periodic typing indicator cleanup (auto-stop stale states) ───
        if (now - last_presence_cleanup >= 5)
        {
            presence_cleanup_stale();
            last_presence_cleanup = now;
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
                        LOG_ERROR_ERRNO("accept");
                    continue;
                }

                conn_t *conn = conn_create(client_fd, epoll_fd);
                if (!conn)
                {
                    LOG_ERROR("conn_create: out of memory");
                    close(client_fd);
                    continue;
                }

                // Send protocol greeting BEFORE setting O_NONBLOCK so send()
                // cannot fail with EAGAIN.  Client reads this byte first to
                // decide whether to attempt TLS.
                char greeting = ssl_ctx ? 'T' : 'P';
                if (send(client_fd, &greeting, 1, MSG_NOSIGNAL) != 1)
                {
                    LOG_ERROR_ERRNO("send greeting");
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
                        LOG_ERROR("TLS SSL_accept failed");
                        conn_free(conn);
                        close(client_fd);
                        continue;
                    }
                }

                LOG_INFO("New client connected (fd=%d%s)", client_fd, ssl_ctx ? ", TLS" : "");

                epoll_add(epoll_fd, conn);
                continue;
            }

            // ── Existing client has data (or disconnected) ────────────────
            conn_t *conn = (conn_t *)events[i].data.ptr;

            if (events[i].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP))
                conn->closing = 1;

            if (threadpool_submit(pool, conn) != 0)
            {
                LOG_ERROR("threadpool_submit failed (queue full?)");
                if (!conn->closing)
                    epoll_rearm(conn);
            }
        }
    }

    // ── Phase 34: Graceful shutdown ─────────────────────────────────────────────
    // Step 1: Stop accepting new connections
    LOG_INFO("Shutting down — stopping accept loop...");
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, server_fd, NULL);

    // Step 2: Notify all connected clients
    LOG_INFO("Notifying connected clients...");
    char msg[MAX_LINE_LEN];
    snprintf(msg, sizeof(msg), "ERR %s\n", ERR_SERVER_SHUTDOWN);
    notify_all_clients(msg);

    // Step 3: Drain — wait for in-flight messages to flush
    struct timespec drain = {0, 500000000L}; // 500ms
    nanosleep(&drain, NULL);

    // Step 4: Destroy thread pool (waits for all workers)
    threadpool_destroy(pool);

    // Step 5: Cleanup Redis/PG connections
    redis_close();
    pg_close();

    close(epoll_fd);
    close(server_fd);

    db_close();
    registry_destroy();

    if (ssl_ctx)
        SSL_CTX_free(ssl_ctx);

    LOG_INFO("Server shut down cleanly.");
}
