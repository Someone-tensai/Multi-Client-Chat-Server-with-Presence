#include "../include/receipt.h"
#include "../include/db.h"
#include "../include/log.h"
#include <sqlite3.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

// ─────────────────────────────────────────────────────────────────────────────
// Read Receipts — SQLite-backed storage
//
// Uses a read_receipts table in the existing chat.db database.
// Since db.c owns the DB connection, we access it via a separate handle
// to avoid conflicts with Janak's db_lock.
// ─────────────────────────────────────────────────────────────────────────────

static sqlite3 *receipt_db = NULL;
static pthread_mutex_t receipt_lock = PTHREAD_MUTEX_INITIALIZER;

// ─────────────────────────────────────────────────────────────────────────────
// Schema for read_receipts
// ─────────────────────────────────────────────────────────────────────────────
static const char *RECEIPT_SCHEMA =
    "CREATE TABLE IF NOT EXISTS read_receipts ("
    "    message_id INTEGER NOT NULL,"
    "    reader     TEXT    NOT NULL,"
    "    read_at    INTEGER NOT NULL,"
    "    PRIMARY KEY (message_id, reader)"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_receipts_msg ON read_receipts(message_id);"
    "CREATE INDEX IF NOT EXISTS idx_receipts_reader ON read_receipts(reader);";

// ─────────────────────────────────────────────────────────────────────────────
// Initialize receipt database
// ─────────────────────────────────────────────────────────────────────────────
int receipt_init(void)
{
    pthread_mutex_lock(&receipt_lock);

    if (sqlite3_open(DB_FILE, &receipt_db) != SQLITE_OK)
    {
        LOG_ERROR("receipt_init: failed to open %s", DB_FILE);
        pthread_mutex_unlock(&receipt_lock);
        return -1;
    }

    char *err = NULL;
    if (sqlite3_exec(receipt_db, RECEIPT_SCHEMA, NULL, NULL, &err) != SQLITE_OK)
    {
        LOG_ERROR("receipt_init schema: %s", err);
        sqlite3_free(err);
        sqlite3_close(receipt_db);
        receipt_db = NULL;
        pthread_mutex_unlock(&receipt_lock);
        return -1;
    }

    pthread_mutex_unlock(&receipt_lock);
    LOG_INFO("Read receipt database initialized");
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mark a message as read by a user
// ─────────────────────────────────────────────────────────────────────────────
int receipt_mark_read(long long msg_id, const char *reader)
{
    if (!receipt_db || msg_id <= 0 || !reader) return -1;

    const char *sql =
        "INSERT OR IGNORE INTO read_receipts (message_id, reader, read_at) "
        "VALUES (?, ?, strftime('%s','now'));";

    pthread_mutex_lock(&receipt_lock);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(receipt_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        LOG_ERROR("receipt_mark_read prepare: %s", sqlite3_errmsg(receipt_db));
        pthread_mutex_unlock(&receipt_lock);
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, msg_id);
    sqlite3_bind_text(stmt, 2, reader, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&receipt_lock);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Check if a user has read a message
// ─────────────────────────────────────────────────────────────────────────────
int receipt_is_read(long long msg_id, const char *reader)
{
    if (!receipt_db || msg_id <= 0 || !reader) return -1;

    const char *sql =
        "SELECT 1 FROM read_receipts WHERE message_id = ? AND reader = ? LIMIT 1;";

    pthread_mutex_lock(&receipt_lock);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(receipt_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        pthread_mutex_unlock(&receipt_lock);
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, msg_id);
    sqlite3_bind_text(stmt, 2, reader, -1, SQLITE_STATIC);

    int found = (sqlite3_step(stmt) == SQLITE_ROW) ? 1 : 0;

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&receipt_lock);

    return found;
}
