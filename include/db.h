#ifndef DB_H
#define DB_H

#include <stddef.h>

// ─────────────────────────────────────────────────────────────────────────────
// Database file path
// ─────────────────────────────────────────────────────────────────────────────
#define DB_FILE "chat.db"

// ─────────────────────────────────────────────────────────────────────────────
// Message row returned from history load
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    char sender[32];
    char text[400];
    long timestamp;
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

// Persist a message to the messages table.
void db_save_message(const char *room, const char *sender, const char *text);

// Load the last `limit` messages for a room into `out`.
// Returns the number of messages actually loaded (0 if none).
int  db_load_history(const char *room, db_message_t *out, int limit);

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

#endif
