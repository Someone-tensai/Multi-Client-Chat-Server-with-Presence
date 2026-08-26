#ifndef SESSION_H
#define SESSION_H

#include <stddef.h>
#include <time.h>
#include <pthread.h>

// ─────────────────────────────────────────────────────────────────────────────
// Session lifecycle — multi-connection User→Session→Connection model
//
// Each LOGIN/Register creates a session with a unique token. Multiple
// connections from the same user can share a session, or each connection
// can have its own session (current design: one session per connection).
//
// Tokens are 32-byte secure random (RAND_bytes) → 64 hex chars.
// Sessions expire after SESSION_TTL seconds (default 24h).
// ─────────────────────────────────────────────────────────────────────────────

#define SESSION_TTL_DEFAULT 86400  // 24 hours
#define MAX_SESSIONS_PER_USER 16

// ─────────────────────────────────────────────────────────────────────────────
// In-memory session record (mirrors DB row)
// ─────────────────────────────────────────────────────────────────────────────
typedef struct session_info {
    char token[65];
    char username[32];
    int  conn_fd;           // file descriptor of the connection that owns this session (-1 if none)
    time_t created_at;
    time_t expires_at;
} session_info_t;

// ─────────────────────────────────────────────────────────────────────────────
// Session error codes
// ─────────────────────────────────────────────────────────────────────────────
typedef enum {
    SESSION_OK = 0,
    SESSION_ERR_NOT_FOUND,
    SESSION_ERR_EXPIRED,
    SESSION_ERR_DB,
    SESSION_ERR_LIMIT,
    SESSION_ERR_INVALID,
} session_err_t;

// ─────────────────────────────────────────────────────────────────────────────
// Session API
// ─────────────────────────────────────────────────────────────────────────────

// Create a new session for `username`. Generates secure token into `token_out`
// (must be >= 65 bytes). Returns SESSION_OK on success.
session_err_t session_create(const char *username, int conn_fd,
                             char *token_out, size_t token_out_size);

// Validate a session token. If valid, copies username into `username_out`.
// On success, optionally refreshes expiry if `refresh` is non-zero.
// Returns SESSION_OK on success, SESSION_ERR_EXPIRED / SESSION_ERR_NOT_FOUND.
session_err_t session_validate(const char *token, char *username_out,
                               size_t username_size, int refresh);

// Revoke (delete) a specific session token.
session_err_t session_revoke(const char *token);

// Revoke all sessions for a given username (used by LOGOUT_ALL).
// Returns number of sessions revoked, or -1 on error.
int session_revoke_all(const char *username);

// Refresh a session's expiry to now + SESSION_TTL.
session_err_t session_refresh(const char *token);

// Cleanup expired sessions. Returns number cleaned or -1 on error.
int session_cleanup_expired(void);

// List active sessions for a given username.
// Fills `out` array up to `max_out` entries. Returns count filled.
int session_list_for_user(const char *username, session_info_t *out, int max_out);

// Check if a specific connection fd has an active session, and return its info.
int session_find_by_fd(int conn_fd, session_info_t *out);

// Associate an existing session token with a new connection fd (used on RECONNECT).
session_err_t session_associate_fd(const char *token, int new_fd);

#endif
