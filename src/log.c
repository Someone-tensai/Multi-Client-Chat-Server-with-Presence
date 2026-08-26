#include "../include/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <stdarg.h>

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static log_level_t current_level = LOG_LEVEL_INFO;
static FILE *log_file_ptr = NULL;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
log_level_t log_level_from_string(const char *str)
{
    if (!str) return LOG_LEVEL_INFO;
    if (strcasecmp(str, "DEBUG") == 0) return LOG_LEVEL_DEBUG;
    if (strcasecmp(str, "INFO") == 0)  return LOG_LEVEL_INFO;
    if (strcasecmp(str, "WARN") == 0)  return LOG_LEVEL_WARN;
    if (strcasecmp(str, "WARNING") == 0) return LOG_LEVEL_WARN;
    if (strcasecmp(str, "ERROR") == 0) return LOG_LEVEL_ERROR;
    if (strcasecmp(str, "NONE") == 0)  return LOG_LEVEL_NONE;
    return LOG_LEVEL_INFO;
}

const char *log_level_to_string(log_level_t level)
{
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_NONE:  return "NONE";
        default: return "INFO";
    }
}

void log_set_level(log_level_t level) {
    pthread_mutex_lock(&log_lock);
    current_level = level;
    pthread_mutex_unlock(&log_lock);
}

log_level_t log_get_level(void) {
    log_level_t lvl;
    pthread_mutex_lock(&log_lock);
    lvl = current_level;
    pthread_mutex_unlock(&log_lock);
    return lvl;
}

int log_init(const char *level_str, const char *file_path)
{
    pthread_mutex_lock(&log_lock);
    current_level = log_level_from_string(level_str);

    if (log_file_ptr) {
        fclose(log_file_ptr);
        log_file_ptr = NULL;
    }

    if (file_path && file_path[0] != '\0') {
        log_file_ptr = fopen(file_path, "a");
        if (!log_file_ptr) {
            // Keep console logging, but report error to stderr
            fprintf(stderr, "[WARN] Failed to open log file '%s': %s\n", file_path, strerror(errno));
            // Do not treat as fatal
        }
    }
    pthread_mutex_unlock(&log_lock);
    return 0;
}

void log_close(void)
{
    pthread_mutex_lock(&log_lock);
    if (log_file_ptr) {
        fclose(log_file_ptr);
        log_file_ptr = NULL;
    }
    pthread_mutex_unlock(&log_lock);
    // Do not destroy log_lock — it may be used after close during shutdown
}

static void log_timestamp(char *buf, size_t size)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tm);
}

void log_write(log_level_t level, const char *file, int line, const char *func, const char *fmt, ...)
{
    if (level < current_level) return;

    char timebuf[32];
    log_timestamp(timebuf, sizeof(timebuf));
    const char *level_str = log_level_to_string(level);

    // Build user message
    char msgbuf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
    va_end(ap);

    pthread_mutex_lock(&log_lock);

    // Console: DEBUG/INFO to stdout, WARN/ERROR to stderr
    FILE *console = (level >= LOG_LEVEL_WARN) ? stderr : stdout;
    fprintf(console, "%s [%s] %s:%d (%s) %s\n", timebuf, level_str, file, line, func, msgbuf);
    fflush(console);

    if (log_file_ptr) {
        fprintf(log_file_ptr, "%s [%s] %s:%d (%s) %s\n", timebuf, level_str, file, line, func, msgbuf);
        fflush(log_file_ptr);
    }

    pthread_mutex_unlock(&log_lock);
}

void log_write_errno(log_level_t level, const char *file, int line, const char *func, const char *msg)
{
    if (level < current_level) return;

    char timebuf[32];
    log_timestamp(timebuf, sizeof(timebuf));
    const char *level_str = log_level_to_string(level);
    int err = errno;
    char errbuf[256];
    snprintf(errbuf, sizeof(errbuf), "%s: %s", msg, strerror(err));

    pthread_mutex_lock(&log_lock);

    FILE *console = stderr;
    fprintf(console, "%s [%s] %s:%d (%s) %s\n", timebuf, level_str, file, line, func, errbuf);
    fflush(console);

    if (log_file_ptr) {
        fprintf(log_file_ptr, "%s [%s] %s:%d (%s) %s\n", timebuf, level_str, file, line, func, errbuf);
        fflush(log_file_ptr);
    }

    pthread_mutex_unlock(&log_lock);
}
