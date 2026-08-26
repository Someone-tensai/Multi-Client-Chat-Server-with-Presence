#include "../include/redis.h"
#include "../include/log.h"
#include <string.h>

#ifdef USE_HIREDIS
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
static redisContext *redis_ctx = NULL;
#else
static void *redis_ctx __attribute__((unused)) = NULL;
#endif

static int redis_enabled = 0;

int redis_init(const redis_config_t *cfg)
{
    if (!cfg || !cfg->enabled) {
        LOG_INFO("Redis disabled — running in standalone mode");
        redis_enabled = 0;
        return 0;
    }

#ifdef USE_HIREDIS
    redis_ctx = redisConnectWithTimeout(cfg->host, cfg->port,
                                         (struct timeval){REDIS_CONNECT_TIMEOUT_SEC, 0});
    if (!redis_ctx || redis_ctx->err) {
        if (redis_ctx) {
            LOG_ERROR("Redis connection error: %s", redis_ctx->errstr);
            redisFree(redis_ctx);
            redis_ctx = NULL;
        } else {
            LOG_ERROR("Redis: can't allocate context");
        }
        redis_enabled = 0;
        return -1;
    }
    redis_enabled = 1;
    LOG_INFO("Redis connected: %s:%d", cfg->host, cfg->port);
    return 0;
#else
    (void)cfg;
    LOG_INFO("Redis support not compiled (install hiredis + rebuild with USE_HIREDIS)");
    redis_enabled = 0;
    return 0;
#endif
}

void redis_close(void)
{
#ifdef USE_HIREDIS
    if (redis_ctx) {
        redisFree(redis_ctx);
        redis_ctx = NULL;
    }
#endif
    redis_enabled = 0;
    LOG_INFO("Redis connection closed");
}

int redis_is_connected(void) { return redis_enabled; }

int redis_publish(const char *channel, const char *message)
{
    if (!redis_enabled || !channel || !message) return -1;
#ifdef USE_HIREDIS
    redisReply *reply = redisCommand(redis_ctx, "PUBLISH %s %s", channel, message);
    if (!reply) return -1;
    int rc = (reply->type != REDIS_REPLY_ERROR) ? 0 : -1;
    freeReplyObject(reply);
    return rc;
#else
    (void)channel; (void)message;
    return -1;
#endif
}

int redis_set(const char *key, const char *value, int expire_sec)
{
    if (!redis_enabled || !key || !value) return -1;
#ifdef USE_HIREDIS
    redisReply *reply;
    if (expire_sec > 0)
        reply = redisCommand(redis_ctx, "SETEX %s %d %s", key, expire_sec, value);
    else
        reply = redisCommand(redis_ctx, "SET %s %s", key, value);
    if (!reply) return -1;
    int rc = (reply->type != REDIS_REPLY_ERROR) ? 0 : -1;
    freeReplyObject(reply);
    return rc;
#else
    (void)key; (void)value; (void)expire_sec;
    return -1;
#endif
}

int redis_get(const char *key, char *out, size_t out_size)
{
    if (!redis_enabled || !key || !out || out_size == 0) return -1;
#ifdef USE_HIREDIS
    redisReply *reply = redisCommand(redis_ctx, "GET %s", key);
    if (!reply) return -1;
    if (reply->type == REDIS_REPLY_STRING) {
        size_t len = reply->len < out_size - 1 ? reply->len : out_size - 1;
        memcpy(out, reply->str, len);
        out[len] = '\0';
        freeReplyObject(reply);
        return 0;
    }
    freeReplyObject(reply);
    return -1;
#else
    (void)key; (void)out; (void)out_size;
    return -1;
#endif
}

int redis_del(const char *key)
{
    if (!redis_enabled || !key) return -1;
#ifdef USE_HIREDIS
    redisReply *reply = redisCommand(redis_ctx, "DEL %s", key);
    if (!reply) return -1;
    freeReplyObject(reply);
    return 0;
#else
    (void)key;
    return -1;
#endif
}

int redis_hset(const char *key, const char *field, const char *value)
{
    if (!redis_enabled || !key || !field || !value) return -1;
#ifdef USE_HIREDIS
    redisReply *reply = redisCommand(redis_ctx, "HSET %s %s %s", key, field, value);
    if (!reply) return -1;
    int rc = (reply->type != REDIS_REPLY_ERROR) ? 0 : -1;
    freeReplyObject(reply);
    return rc;
#else
    (void)key; (void)field; (void)value;
    return -1;
#endif
}

int redis_hget(const char *key, const char *field, char *out, size_t out_size)
{
    if (!redis_enabled || !key || !field || !out || out_size == 0) return -1;
#ifdef USE_HIREDIS
    redisReply *reply = redisCommand(redis_ctx, "HGET %s %s", key, field);
    if (!reply) return -1;
    if (reply->type == REDIS_REPLY_STRING) {
        size_t len = reply->len < out_size - 1 ? reply->len : out_size - 1;
        memcpy(out, reply->str, len);
        out[len] = '\0';
        freeReplyObject(reply);
        return 0;
    }
    freeReplyObject(reply);
    return -1;
#else
    (void)key; (void)field; (void)out; (void)out_size;
    return -1;
#endif
}

int redis_hdel(const char *key, const char *field)
{
    if (!redis_enabled || !key || !field) return -1;
#ifdef USE_HIREDIS
    redisReply *reply = redisCommand(redis_ctx, "HDEL %s %s", key, field);
    if (!reply) return -1;
    freeReplyObject(reply);
    return 0;
#else
    (void)key; (void)field;
    return -1;
#endif
}

int redis_sadd(const char *key, const char *member)
{
    if (!redis_enabled || !key || !member) return -1;
#ifdef USE_HIREDIS
    redisReply *reply = redisCommand(redis_ctx, "SADD %s %s", key, member);
    if (!reply) return -1;
    freeReplyObject(reply);
    return 0;
#else
    (void)key; (void)member;
    return -1;
#endif
}

int redis_srem(const char *key, const char *member)
{
    if (!redis_enabled || !key || !member) return -1;
#ifdef USE_HIREDIS
    redisReply *reply = redisCommand(redis_ctx, "SREM %s %s", key, member);
    if (!reply) return -1;
    freeReplyObject(reply);
    return 0;
#else
    (void)key; (void)member;
    return -1;
#endif
}

int redis_smembers(const char *key, char out[][64], int max_out)
{
    if (!redis_enabled || !key || !out || max_out <= 0) return 0;
#ifdef USE_HIREDIS
    redisReply *reply = redisCommand(redis_ctx, "SMEMBERS %s", key);
    if (!reply || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return 0;
    }
    int count = 0;
    for (size_t i = 0; i < reply->elements && count < max_out; i++) {
        strncpy(out[count], reply->element[i]->str, 63);
        out[count][63] = '\0';
        count++;
    }
    freeReplyObject(reply);
    return count;
#else
    (void)key; (void)out; (void)max_out;
    return 0;
#endif
}
