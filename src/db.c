#include "../include/db.h"
#include "../include/log.h"
#include <sqlite3.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

// ─────────────────────────────────────────────────────────────────────────────
// Single global DB connection + its own mutex
// All public db_* functions lock this before touching SQLite.
// (SQLite can be compiled in serialised mode but we don't rely on that.)
// ─────────────────────────────────────────────────────────────────────────────
static sqlite3          *db   = NULL;
static pthread_mutex_t   db_lock = PTHREAD_MUTEX_INITIALIZER;

// ─────────────────────────────────────────────────────────────────────────────
// Schema
// ─────────────────────────────────────────────────────────────────────────────
static const char *SCHEMA =
    "CREATE TABLE IF NOT EXISTS messages ("
    "    id        INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    room      TEXT    NOT NULL,"
    "    sender    TEXT    NOT NULL,"
    "    text      TEXT    NOT NULL,"
    "    timestamp INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_messages_room ON messages(room, id);"

    "CREATE TABLE IF NOT EXISTS users ("
    "    username      TEXT PRIMARY KEY,"
    "    password_hash TEXT NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS sessions ("
    "    token      TEXT PRIMARY KEY,"
    "    username   TEXT NOT NULL,"
    "    created_at INTEGER NOT NULL,"
    "    expires_at INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_sessions_username ON sessions(username);"
    "CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions(expires_at);";

// ─────────────────────────────────────────────────────────────────────────────
// Internal helper — hex-encode a SHA-256 digest into a 65-byte string
// ─────────────────────────────────────────────────────────────────────────────
static void sha256_hex(const char *input, char out[65])
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char *)input, strlen(input), digest);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        snprintf(out + i * 2, 3, "%02x", digest[i]);
    out[64] = '\0';
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
int db_open(void)
{
    pthread_mutex_lock(&db_lock);

    if (sqlite3_open(DB_FILE, &db) != SQLITE_OK)
    {
        LOG_ERROR("db_open: %s", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_lock);
        return -1;
    }

    // Enable WAL mode — better concurrent read performance
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    // Enforce foreign keys (good practice)
    sqlite3_exec(db, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);

    char *err = NULL;
    if (sqlite3_exec(db, SCHEMA, NULL, NULL, &err) != SQLITE_OK)
    {
        LOG_ERROR("db_open schema: %s", err);
        sqlite3_free(err);
        pthread_mutex_unlock(&db_lock);
        return -1;
    }

    pthread_mutex_unlock(&db_lock);
    LOG_INFO("Database opened: %s", DB_FILE);
    return 0;
}

void db_close(void)
{
    pthread_mutex_lock(&db_lock);
    if (db)
    {
        sqlite3_close(db);
        db = NULL;
    }
    pthread_mutex_unlock(&db_lock);
}

// ─────────────────────────────────────────────────────────────────────────────
// Message history — save
// ─────────────────────────────────────────────────────────────────────────────
void db_save_message(const char *room, const char *sender, const char *text)
{
    if (!db || !room || !sender || !text) return;

    const char *sql =
        "INSERT INTO messages (room, sender, text, timestamp) VALUES (?, ?, ?, strftime('%s','now'));";

    pthread_mutex_lock(&db_lock);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        LOG_ERROR("db_save_message prepare: %s", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_lock);
        return;
    }

    sqlite3_bind_text(stmt, 1, room,   -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, sender, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, text,   -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE)
        LOG_ERROR("db_save_message step: %s", sqlite3_errmsg(db));

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_lock);
}

// ─────────────────────────────────────────────────────────────────────────────
// Message history — load last N messages for a room
// ─────────────────────────────────────────────────────────────────────────────
int db_load_history(const char *room, db_message_t *out, int limit)
{
    if (!db || !room || !out || limit <= 0) return 0;

    // Subquery so we get the last N rows in chronological order
    const char *sql =
        "SELECT sender, text, timestamp FROM ("
        "  SELECT sender, text, timestamp, id FROM messages WHERE room = ?"
        "  ORDER BY id DESC LIMIT ?"
        ") ORDER BY id ASC;";

    pthread_mutex_lock(&db_lock);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        LOG_ERROR("db_load_history prepare: %s", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    sqlite3_bind_text(stmt, 1, room,  -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 2, limit);

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < limit)
    {
        const char *sender = (const char *)sqlite3_column_text(stmt, 0);
        const char *text   = (const char *)sqlite3_column_text(stmt, 1);
        long ts            = (long)sqlite3_column_int64(stmt, 2);

        strncpy(out[count].sender, sender ? sender : "", sizeof(out[count].sender) - 1);
        strncpy(out[count].text,   text   ? text   : "", sizeof(out[count].text)   - 1);
        out[count].sender[sizeof(out[count].sender) - 1] = '\0';
        out[count].text  [sizeof(out[count].text)   - 1] = '\0';
        out[count].timestamp = ts;
        count++;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_lock);
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Auth — register new user (SHA-256 hashed password)
// ─────────────────────────────────────────────────────────────────────────────
int db_register_user(const char *username, const char *password)
{
    if (!db || !username || !password) return -2;

    char hash[65];
    sha256_hex(password, hash);

    const char *sql =
        "INSERT INTO users (username, password_hash) VALUES (?, ?);";

    pthread_mutex_lock(&db_lock);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        pthread_mutex_unlock(&db_lock);
        return -2;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hash,     -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_lock);

    if (rc == SQLITE_CONSTRAINT) return -1;  // username already exists
    if (rc != SQLITE_DONE)       return -2;  // DB error
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Auth — verify login
// ─────────────────────────────────────────────────────────────────────────────
int db_verify_user(const char *username, const char *password)
{
    if (!db || !username || !password) return -3;

    const char *sql =
        "SELECT password_hash FROM users WHERE username = ?;";

    pthread_mutex_lock(&db_lock);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        pthread_mutex_unlock(&db_lock);
        return -3;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    int result;
    if (sqlite3_step(stmt) != SQLITE_ROW)
    {
        // No row — user not found
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_lock);
        return -1;
    }

    const char *stored_hash = (const char *)sqlite3_column_text(stmt, 0);
    char input_hash[65];
    sha256_hex(password, input_hash);

    result = (strcmp(stored_hash, input_hash) == 0) ? 0 : -2;

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_lock);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Auth — check existence only (used in REGISTER to decide new vs existing)
// ─────────────────────────────────────────────────────────────────────────────
int db_user_exists(const char *username)
{
    if (!db || !username) return 0;

    const char *sql = "SELECT 1 FROM users WHERE username = ? LIMIT 1;";

    pthread_mutex_lock(&db_lock);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    int exists = (sqlite3_step(stmt) == SQLITE_ROW) ? 1 : 0;

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_lock);
    return exists;
}

// ─────────────────────────────────────────────────────────────────────────────
// Session handling — secure token generation
// ─────────────────────────────────────────────────────────────────────────────
int db_generate_token(char *out, size_t out_size)
{
    if (!out || out_size < 65) return -1;
    unsigned char rand_bytes[32];
    if (RAND_bytes(rand_bytes, sizeof(rand_bytes)) != 1) {
        LOG_ERROR("RAND_bytes failed for session token");
        return -1;
    }
    for (int i = 0; i < 32; i++) {
        snprintf(out + i*2, out_size - (size_t)(i*2), "%02x", rand_bytes[i]);
    }
    out[64] = '\0';
    return 0;
}

int db_create_session(const char *username, char *token_out, size_t token_out_size)
{
    if (!db || !username || !token_out || token_out_size < 65) return -1;

    char token[65];
    if (db_generate_token(token, sizeof(token)) != 0) return -1;

    const char *sql = "INSERT INTO sessions (token, username, created_at, expires_at) VALUES (?, ?, strftime('%s','now'), strftime('%s','now') + ?);";

    pthread_mutex_lock(&db_lock);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db_lock);
        LOG_ERROR("db_create_session prepare: %s", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, token, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, SESSION_EXPIRE_SEC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_lock);

    if (rc == SQLITE_CONSTRAINT) {
        // Extremely unlikely token collision — retry once
        LOG_WARN("Session token collision, retrying");
        return db_create_session(username, token_out, token_out_size);
    }
    if (rc != SQLITE_DONE) {
        LOG_ERROR("db_create_session step: %s", sqlite3_errmsg(db));
        return -1;
    }

    strncpy(token_out, token, token_out_size - 1);
    token_out[token_out_size - 1] = '\0';
    return 0;
}

int db_validate_session(const char *token, char *username_out, size_t username_size)
{
    if (!db || !token || !username_out) return -3;
    if (strlen(token) == 0) return -1;

    const char *sql = "SELECT username, expires_at FROM sessions WHERE token = ?;";

    pthread_mutex_lock(&db_lock);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db_lock);
        return -3;
    }

    sqlite3_bind_text(stmt, 1, token, -1, SQLITE_STATIC);

    int step = sqlite3_step(stmt);
    if (step != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_lock);
        return -1; // not found
    }

    const char *username = (const char *)sqlite3_column_text(stmt, 0);
    long expires_at = (long)sqlite3_column_int64(stmt, 1);
    long now = (long)time(NULL);

    if (expires_at < now) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_lock);
        // Expired — clean it up
        db_delete_session(token);
        return -2;
    }

    if (username) {
        strncpy(username_out, username, username_size - 1);
        username_out[username_size - 1] = '\0';
    } else {
        username_out[0] = '\0';
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_lock);
    return 0;
}

int db_delete_session(const char *token)
{
    if (!db || !token) return -1;
    const char *sql = "DELETE FROM sessions WHERE token = ?;";

    pthread_mutex_lock(&db_lock);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db_lock);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, token, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_lock);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_cleanup_expired_sessions(void)
{
    if (!db) return -1;
    const char *sql = "DELETE FROM sessions WHERE expires_at < strftime('%s','now');";

    pthread_mutex_lock(&db_lock);
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    int changes = sqlite3_changes(db);
    if (err) {
        LOG_WARN("db_cleanup_expired_sessions: %s", err);
        sqlite3_free(err);
    }
    pthread_mutex_unlock(&db_lock);
    return (rc == SQLITE_OK) ? changes : -1;
}
