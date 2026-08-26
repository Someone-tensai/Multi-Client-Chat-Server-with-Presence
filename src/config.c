#include "../include/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ─────────────────────────────────────────────────────────────────────────────
// Global config instance
// ─────────────────────────────────────────────────────────────────────────────
static server_config_t global_cfg;

server_config_t *config_get(void)
{
    return &global_cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

// Skip leading/trailing whitespace in place (returns pointer into buf).
static char *trim(char *buf)
{
    while (isspace((unsigned char)*buf)) buf++;
    char *end = buf + strlen(buf);
    while (end > buf && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Parser — handles lines of the form: key = value
//   - Lines starting with '#' or '[' are comments
//   - Blank lines are ignored
//   - Unknown keys are silently skipped (forward-compatible)
// ─────────────────────────────────────────────────────────────────────────────
int config_load(server_config_t *cfg, const char *path)
{
    // Start with compile-time defaults
    cfg->port               = CFG_DEFAULT_PORT;
    cfg->thread_pool_size   = CFG_DEFAULT_THREAD_POOL_SIZE;
    cfg->max_clients        = CFG_DEFAULT_MAX_CLIENTS;
    cfg->max_rooms          = CFG_DEFAULT_MAX_ROOMS;
    cfg->max_members        = CFG_DEFAULT_MAX_MEMBERS;
    cfg->history_size       = CFG_DEFAULT_HISTORY_SIZE;
    cfg->rate_bucket_max    = CFG_DEFAULT_RATE_BUCKET_MAX;
    cfg->rate_refill_rate   = CFG_DEFAULT_RATE_REFILL_RATE;
    cfg->rate_msg_cost      = CFG_DEFAULT_RATE_MSG_COST;
    strncpy(cfg->tls_cert, CFG_DEFAULT_TLS_CERT, sizeof(cfg->tls_cert) - 1);
    cfg->tls_cert[sizeof(cfg->tls_cert)-1] = '\0';
    strncpy(cfg->tls_key,  CFG_DEFAULT_TLS_KEY,  sizeof(cfg->tls_key) - 1);
    cfg->tls_key[sizeof(cfg->tls_key)-1] = '\0';
    cfg->pool_shrink_idle_sec = CFG_DEFAULT_POOL_SHRINK_IDLE;
    cfg->pool_min_threads   = CFG_DEFAULT_POOL_MIN_THREADS;
    strncpy(cfg->log_level, CFG_DEFAULT_LOG_LEVEL, sizeof(cfg->log_level)-1);
    cfg->log_level[sizeof(cfg->log_level)-1] = '\0';
    cfg->log_file[0] = '\0';

    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[512];
    while (fgets(line, sizeof(line), fp))
    {
        char *raw = trim(line);

        // Skip empty lines and comments
        if (*raw == '\0' || *raw == '#' || *raw == '[')
            continue;

        // Split on '='
        char *eq = strchr(raw, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = trim(raw);
        char *val = trim(eq + 1);

        // Match keys
        if (strcmp(key, "port") == 0)
            cfg->port = atoi(val);
        else if (strcmp(key, "thread_pool_size") == 0)
            cfg->thread_pool_size = atoi(val);
        else if (strcmp(key, "max_clients") == 0)
            cfg->max_clients = atoi(val);
        else if (strcmp(key, "max_rooms") == 0)
            cfg->max_rooms = atoi(val);
        else if (strcmp(key, "max_members") == 0)
            cfg->max_members = atoi(val);
        else if (strcmp(key, "history_size") == 0)
            cfg->history_size = atoi(val);
        else if (strcmp(key, "rate_bucket_max") == 0)
            cfg->rate_bucket_max = atoi(val);
        else if (strcmp(key, "rate_refill_rate") == 0)
            cfg->rate_refill_rate = atof(val);
        else if (strcmp(key, "rate_msg_cost") == 0)
            cfg->rate_msg_cost = atoi(val);
        else if (strcmp(key, "tls_cert") == 0)
            strncpy(cfg->tls_cert, val, sizeof(cfg->tls_cert) - 1);
        else if (strcmp(key, "tls_key") == 0)
            strncpy(cfg->tls_key, val, sizeof(cfg->tls_key) - 1);
        else if (strcmp(key, "pool_shrink_idle_sec") == 0)
            cfg->pool_shrink_idle_sec = atoi(val);
        else if (strcmp(key, "pool_min_threads") == 0)
            cfg->pool_min_threads = atoi(val);
        else if (strcmp(key, "log_level") == 0)
            strncpy(cfg->log_level, val, sizeof(cfg->log_level)-1);
        else if (strcmp(key, "log_file") == 0)
            strncpy(cfg->log_file, val, sizeof(cfg->log_file)-1);
    }

    fclose(fp);
    return 0;
}
