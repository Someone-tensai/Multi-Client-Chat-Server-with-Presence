#include "../include/metrics.h"
#include <stdio.h>
#include <time.h>

static metrics_t g_metrics;

void metrics_init(void)
{
    atomic_store(&g_metrics.total_connections, 0);
    atomic_store(&g_metrics.active_connections, 0);
    atomic_store(&g_metrics.total_messages, 0);
    atomic_store(&g_metrics.total_pm, 0);
    atomic_store(&g_metrics.total_rooms_created, 0);
    atomic_store(&g_metrics.total_logins, 0);
    atomic_store(&g_metrics.total_failed_logins, 0);
    atomic_store(&g_metrics.total_reconnects, 0);
    atomic_store(&g_metrics.total_kicks, 0);
    atomic_store(&g_metrics.total_bans, 0);
    atomic_store(&g_metrics.uptime_start, (long long)time(NULL));
}

metrics_t *metrics_get(void) { return &g_metrics; }

void metrics_increment_connections(void) { atomic_fetch_add(&g_metrics.total_connections, 1); atomic_fetch_add(&g_metrics.active_connections, 1); }
void metrics_decrement_connections(void) { atomic_fetch_sub(&g_metrics.active_connections, 1); }
void metrics_increment_messages(void)    { atomic_fetch_add(&g_metrics.total_messages, 1); }
void metrics_increment_pm(void)          { atomic_fetch_add(&g_metrics.total_pm, 1); }
void metrics_increment_rooms(void)       { atomic_fetch_add(&g_metrics.total_rooms_created, 1); }
void metrics_increment_logins(void)      { atomic_fetch_add(&g_metrics.total_logins, 1); }
void metrics_increment_failed_logins(void) { atomic_fetch_add(&g_metrics.total_failed_logins, 1); }
void metrics_increment_reconnects(void)  { atomic_fetch_add(&g_metrics.total_reconnects, 1); }
void metrics_increment_kicks(void)       { atomic_fetch_add(&g_metrics.total_kicks, 1); }
void metrics_increment_bans(void)        { atomic_fetch_add(&g_metrics.total_bans, 1); }

void metrics_format_prometheus(char *out, size_t out_size)
{
    long long now = (long long)time(NULL);
    long long uptime = now - atomic_load(&g_metrics.uptime_start);

    int off = 0;
    off += snprintf(out + off, out_size - off, "# HELP chat_connections_total Total connections accepted\n");
    off += snprintf(out + off, out_size - off, "# TYPE chat_connections_total counter\n");
    off += snprintf(out + off, out_size - off, "chat_connections_total %lld\n", atomic_load(&g_metrics.total_connections));

    off += snprintf(out + off, out_size - off, "# HELP chat_active_connections Currently active connections\n");
    off += snprintf(out + off, out_size - off, "# TYPE chat_active_connections gauge\n");
    off += snprintf(out + off, out_size - off, "chat_active_connections %lld\n", atomic_load(&g_metrics.active_connections));

    off += snprintf(out + off, out_size - off, "# HELP chat_messages_total Total messages sent\n");
    off += snprintf(out + off, out_size - off, "# TYPE chat_messages_total counter\n");
    off += snprintf(out + off, out_size - off, "chat_messages_total %lld\n", atomic_load(&g_metrics.total_messages));

    off += snprintf(out + off, out_size - off, "# HELP chat_pm_total Total private messages sent\n");
    off += snprintf(out + off, out_size - off, "# TYPE chat_pm_total counter\n");
    off += snprintf(out + off, out_size - off, "chat_pm_total %lld\n", atomic_load(&g_metrics.total_pm));

    off += snprintf(out + off, out_size - off, "# HELP chat_rooms_total Total rooms created\n");
    off += snprintf(out + off, out_size - off, "# TYPE chat_rooms_total counter\n");
    off += snprintf(out + off, out_size - off, "chat_rooms_total %lld\n", atomic_load(&g_metrics.total_rooms_created));

    off += snprintf(out + off, out_size - off, "# HELP chat_logins_total Total logins\n");
    off += snprintf(out + off, out_size - off, "# TYPE chat_logins_total counter\n");
    off += snprintf(out + off, out_size - off, "chat_logins_total %lld\n", atomic_load(&g_metrics.total_logins));

    off += snprintf(out + off, out_size - off, "# HELP chat_failed_logins_total Total failed logins\n");
    off += snprintf(out + off, out_size - off, "# TYPE chat_failed_logins_total counter\n");
    off += snprintf(out + off, out_size - off, "chat_failed_logins_total %lld\n", atomic_load(&g_metrics.total_failed_logins));

    off += snprintf(out + off, out_size - off, "# HELP chat_uptime_seconds Server uptime\n");
    off += snprintf(out + off, out_size - off, "# TYPE chat_uptime_seconds gauge\n");
    off += snprintf(out + off, out_size - off, "chat_uptime_seconds %lld\n", uptime);
}
