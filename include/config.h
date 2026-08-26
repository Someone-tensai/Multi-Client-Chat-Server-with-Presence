#ifndef CONFIG_H
#define CONFIG_H

// ─────────────────────────────────────────────────────────────────────────────
// Compile-time defaults (used when server.conf is absent or a key is missing)
// ─────────────────────────────────────────────────────────────────────────────
#define CFG_DEFAULT_PORT               8888
#define CFG_DEFAULT_THREAD_POOL_SIZE   16
#define CFG_DEFAULT_MAX_CLIENTS        64
#define CFG_DEFAULT_MAX_ROOMS          16
#define CFG_DEFAULT_MAX_MEMBERS        16
#define CFG_DEFAULT_HISTORY_SIZE       10
#define CFG_DEFAULT_RATE_BUCKET_MAX    5
#define CFG_DEFAULT_RATE_REFILL_RATE   1.0
#define CFG_DEFAULT_RATE_MSG_COST      1
#define CFG_DEFAULT_TLS_CERT           "server.crt"
#define CFG_DEFAULT_TLS_KEY            "server.key"
#define CFG_DEFAULT_POOL_SHRINK_IDLE   30
#define CFG_DEFAULT_POOL_MIN_THREADS   2

// ─────────────────────────────────────────────────────────────────────────────
// Runtime config struct — one global instance populated at startup
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    int    port;
    int    thread_pool_size;
    int    max_clients;
    int    max_rooms;
    int    max_members;
    int    history_size;
    int    rate_bucket_max;
    double rate_refill_rate;
    int    rate_msg_cost;
    char   tls_cert[256];
    char   tls_key[256];
    int    pool_shrink_idle_sec;
    int    pool_min_threads;
} server_config_t;

// ─────────────────────────────────────────────────────────────────────────────
// API
// ─────────────────────────────────────────────────────────────────────────────

// Parse a key=value config file and populate cfg with parsed values.
// Missing keys retain their compile-time defaults.
// Returns 0 on success, -1 if the file cannot be opened.
int config_load(server_config_t *cfg, const char *path);

// Return a pointer to the global config (valid after config_load).
server_config_t *config_get(void);

#endif
