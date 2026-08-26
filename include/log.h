#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

// ─────────────────────────────────────────────────────────────────────────────
// Log levels — ordered by severity
// ─────────────────────────────────────────────────────────────────────────────
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_NONE  = 4  // disable all
} log_level_t;

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
// Initialize logger with level string (e.g. "INFO", "DEBUG") and optional file path.
// If level_str is NULL or invalid, defaults to INFO.
// If file_path is NULL or empty, logs to console only (stderr/stdout).
// Returns 0 on success, -1 on failure (file open error — will still log to console).
int log_init(const char *level_str, const char *file_path);

// Close log file and destroy mutex.
void log_close(void);

// Convert level string to enum; case-insensitive. Returns LOG_LEVEL_INFO on unknown.
log_level_t log_level_from_string(const char *str);
const char *log_level_to_string(log_level_t level);

// Set minimum level at runtime
void log_set_level(log_level_t level);
log_level_t log_get_level(void);

// ─────────────────────────────────────────────────────────────────────────────
// Core logging functions — thread-safe
// ─────────────────────────────────────────────────────────────────────────────
void log_write(log_level_t level, const char *file, int line, const char *func, const char *fmt, ...);
void log_write_errno(log_level_t level, const char *file, int line, const char *func, const char *msg);

// ─────────────────────────────────────────────────────────────────────────────
// Convenience macros — include file/line/func automatically
// ─────────────────────────────────────────────────────────────────────────────
#define LOG_DEBUG(...) log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_INFO(...)  log_write(LOG_LEVEL_INFO,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_WARN(...)  log_write(LOG_LEVEL_WARN,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERROR(...) log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)

// With errno string appended
#define LOG_ERROR_ERRNO(msg) log_write_errno(LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, msg)

#endif
