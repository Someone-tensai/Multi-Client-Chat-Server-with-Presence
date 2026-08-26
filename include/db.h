#ifndef DB_H
#define DB_H

#include <stddef.h>

// ─────────────────────────────────────────────────────────────────────────────
// Database file path
// ─────────────────────────────────────────────────────────────────────────────
#define DB_FILE "chat.db"

// ─────────────────────────────────────────────────────────────────────────────
// Message row returned from history load — now includes id + deleted/edited
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    long long id;
    char sender[32];
    char text[400];
    long timestamp;
    int deleted;
    long edited_at;
} db_message_t;

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

// Open (or create) the database and create tables if they don't exist.
// Must be called once at server startup before any other db_* calls.
// Returns 0 on success, -1 on failure.
int  db_open(void);

// Close the database. Call on server shutdown.
void db_close(void);

// ─────────────────────────────────────────────────────────────────────────────
// Message history
// ─────────────────────────────────────────────────────────────────────────────

// Persist a message to the messages table. Returns DB id (>0) on success, -1 on failure.
long long db_save_message(const char *room, const char *sender, const char *text);

// Load the last `limit` messages for a room into `out`.
// Returns the number of messages actually loaded (0 if none).
int  db_load_history(const char *room, db_message_t *out, int limit);

// Cursor-based history: load messages before cursor (exclusive) ordered by id DESC, limit.
// If cursor <=0, starts from latest. Returns count, next_cursor via out param.
int db_load_history_before(const char *room, long long before_id, int limit, db_message_t *out, long long *next_cursor);

// Update message text (edit). Only author or admin should call. Returns 0 on success.
int db_edit_message(long long msg_id, const char *new_text);

// Soft-delete message (retain row, set deleted=1). Returns 0 on success.
int db_delete_message(long long msg_id);

// ─────────────────────────────────────────────────────────────────────────────
// User authentication
// ─────────────────────────────────────────────────────────────────────────────

// Register a new user with a hashed password.
// Returns  0 — success
//         -1 — username already exists
//         -2 — DB error
int  db_register_user(const char *username, const char *password);

// Check a login attempt.
// Returns  0 — password matches
//         -1 — user not found
//         -2 — wrong password
//         -3 — DB error
int  db_verify_user(const char *username, const char *password);

// Returns 1 if username exists in the users table, 0 otherwise.
int  db_user_exists(const char *username);

// ─────────────────────────────────────────────────────────────────────────────
// Session handling (SQLite-backed, secure random tokens)
// ─────────────────────────────────────────────────────────────────────────────
#define SESSION_TOKEN_HEX_LEN 64  // 32 bytes random -> 64 hex chars
#define SESSION_EXPIRE_SEC 86400  // 24h default

// Create a new session for username, generate secure token into token_out (must be at least 65 bytes).
// Returns 0 on success, -1 on DB error.
int db_create_session(const char *username, char *token_out, size_t token_out_size);

// Validate a session token. If valid, copy username into username_out and return 0.
// Returns -1 if token not found, -2 if expired, -3 on DB error.
// On success, optionally rotates token if new_token_out is provided (non-NULL): old token is deleted and new token created.
int db_validate_session(const char *token, char *username_out, size_t username_size);

// Delete a session (e.g. on logout or token rotation)
int db_delete_session(const char *token);

// Delete all expired sessions, returns number deleted or -1 on error
int db_cleanup_expired_sessions(void);

// Helper: generate secure random token (hex) into out (size must be >=65). Returns 0 on success.
int db_generate_token(char *out, size_t out_size);

// ─────────────────────────────────────────────────────────────────────────────
// Room persistence
// ─────────────────────────────────────────────────────────────────────────────
int db_create_room(const char *room_name, const char *owner);
int db_delete_room(const char *room_name);
int db_load_rooms(char rooms[][32], int max_rooms); // returns count, fills array
int db_search_messages(const char *room, const char *query, int limit, db_message_t *out);

#endif
