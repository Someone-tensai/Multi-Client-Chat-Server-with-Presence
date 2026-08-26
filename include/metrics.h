#ifndef METRICS_H
#define METRICS_H

#include <stddef.h>
#include <stdatomic.h>

typedef struct metrics {
    _Atomic long long total_connections;
    _Atomic long long active_connections;
    _Atomic long long total_messages;
    _Atomic long long total_pm;
    _Atomic long long total_rooms_created;
    _Atomic long long total_logins;
    _Atomic long long total_failed_logins;
    _Atomic long long total_reconnects;
    _Atomic long long total_kicks;
    _Atomic long long total_bans;
    _Atomic long long uptime_start;
} metrics_t;

void metrics_init(void);
metrics_t *metrics_get(void);
void metrics_increment_connections(void);
void metrics_decrement_connections(void);
void metrics_increment_messages(void);
void metrics_increment_pm(void);
void metrics_increment_rooms(void);
void metrics_increment_logins(void);
void metrics_increment_failed_logins(void);
void metrics_increment_reconnects(void);
void metrics_increment_kicks(void);
void metrics_increment_bans(void);
void metrics_format_prometheus(char *out, size_t out_size);

#endif
