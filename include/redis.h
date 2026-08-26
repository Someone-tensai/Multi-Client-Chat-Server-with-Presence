#ifndef REDIS_H
#define REDIS_H

#include <stddef.h>

#define REDIS_CONNECT_TIMEOUT_SEC 5

typedef struct redis_config {
    char host[256];
    int  port;
    int  enabled;
} redis_config_t;

int  redis_init(const redis_config_t *cfg);
void redis_close(void);
int  redis_is_connected(void);

int  redis_publish(const char *channel, const char *message);
int  redis_set(const char *key, const char *value, int expire_sec);
int  redis_get(const char *key, char *out, size_t out_size);
int  redis_del(const char *key);
int  redis_hset(const char *key, const char *field, const char *value);
int  redis_hget(const char *key, const char *field, char *out, size_t out_size);
int  redis_hdel(const char *key, const char *field);
int  redis_sadd(const char *key, const char *member);
int  redis_srem(const char *key, const char *member);
int  redis_smembers(const char *key, char out[][64], int max_out);

#endif
