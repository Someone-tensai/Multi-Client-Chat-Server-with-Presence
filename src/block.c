#include "../include/block.h"
#include "../include/db.h"
#include "../include/log.h"
#include <sqlite3.h>
#include <string.h>
#include <pthread.h>

static sqlite3 *block_db = NULL;
static pthread_mutex_t block_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *BLOCK_SCHEMA =
    "CREATE TABLE IF NOT EXISTS blocks ("
    "    blocker TEXT NOT NULL,"
    "    blocked TEXT NOT NULL,"
    "    created_at INTEGER NOT NULL,"
    "    PRIMARY KEY (blocker, blocked)"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_blocks_blocker ON blocks(blocker);"
    "CREATE INDEX IF NOT EXISTS idx_blocks_blocked ON blocks(blocked);"
    "CREATE TABLE IF NOT EXISTS mutes ("
    "    room_name TEXT NOT NULL,"
    "    muted_by  TEXT NOT NULL,"
    "    muted_user TEXT NOT NULL,"
    "    created_at INTEGER NOT NULL,"
    "    PRIMARY KEY (room_name, muted_user)"
    ");"
    "CREATE TABLE IF NOT EXISTS bans ("
    "    room_name TEXT NOT NULL,"
    "    banned_by TEXT NOT NULL,"
    "    banned_user TEXT NOT NULL,"
    "    created_at INTEGER NOT NULL,"
    "    PRIMARY KEY (room_name, banned_user)"
    ");";

static int ensure_block_db(void)
{
    if (block_db) return 0;
    pthread_mutex_lock(&block_lock);
    if (block_db) { pthread_mutex_unlock(&block_lock); return 0; }
    if (sqlite3_open(DB_FILE, &block_db) != SQLITE_OK) {
        LOG_ERROR("block_db: failed to open %s", DB_FILE);
        pthread_mutex_unlock(&block_lock);
        return -1;
    }
    char *err = NULL;
    if (sqlite3_exec(block_db, BLOCK_SCHEMA, NULL, NULL, &err) != SQLITE_OK) {
        LOG_ERROR("block_db schema: %s", err);
        sqlite3_free(err);
        sqlite3_close(block_db);
        block_db = NULL;
        pthread_mutex_unlock(&block_lock);
        return -1;
    }
    pthread_mutex_unlock(&block_lock);
    LOG_INFO("Block/mute/ban database initialized");
    return 0;
}

int block_init(void) { return ensure_block_db(); }

int block_add(const char *blocker, const char *blocked)
{
    if (!blocker || !blocked || strcmp(blocker, blocked) == 0) return -1;
    if (ensure_block_db() != 0) return -1;
    const char *sql = "INSERT OR IGNORE INTO blocks (blocker, blocked, created_at) VALUES (?, ?, strftime('%s','now'));";
    pthread_mutex_lock(&block_lock);
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(block_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&block_lock);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, blocker, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, blocked, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&block_lock);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int block_remove(const char *blocker, const char *blocked)
{
    if (!blocker || !blocked) return -1;
    if (ensure_block_db() != 0) return -1;
    const char *sql = "DELETE FROM blocks WHERE blocker = ? AND blocked = ?;";
    pthread_mutex_lock(&block_lock);
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(block_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&block_lock);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, blocker, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, blocked, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(block_db);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&block_lock);
    return (rc == SQLITE_DONE && changes > 0) ? 0 : -1;
}

int block_check(const char *blocker, const char *blocked)
{
    if (!blocker || !blocked) return 0;
    if (ensure_block_db() != 0) return 0;
    const char *sql = "SELECT 1 FROM blocks WHERE blocker = ? AND blocked = ? LIMIT 1;";
    pthread_mutex_lock(&block_lock);
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(block_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&block_lock);
        return 0;
    }
    sqlite3_bind_text(stmt, 1, blocker, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, blocked, -1, SQLITE_STATIC);
    int found = (sqlite3_step(stmt) == SQLITE_ROW) ? 1 : 0;
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&block_lock);
    return found;
}

int block_list(const char *blocker, char out[][32], int max_out)
{
    if (!blocker || !out || max_out <= 0) return 0;
    if (ensure_block_db() != 0) return 0;
    const char *sql = "SELECT blocked FROM blocks WHERE blocker = ? LIMIT ?;";
    pthread_mutex_lock(&block_lock);
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(block_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&block_lock);
        return 0;
    }
    sqlite3_bind_text(stmt, 1, blocker, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, max_out);
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_out) {
        const char *b = (const char *)sqlite3_column_text(stmt, 0);
        strncpy(out[count], b ? b : "", 31);
        out[count][31] = '\0';
        count++;
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&block_lock);
    return count;
}

int block_count(const char *blocker)
{
    if (!blocker) return 0;
    if (ensure_block_db() != 0) return 0;
    const char *sql = "SELECT COUNT(*) FROM blocks WHERE blocker = ?;";
    pthread_mutex_lock(&block_lock);
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(block_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&block_lock);
        return 0;
    }
    sqlite3_bind_text(stmt, 1, blocker, -1, SQLITE_STATIC);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&block_lock);
    return count;
}

int mute_add(const char *room_name, const char *muted_by, const char *muted_user)
{
    if (!room_name || !muted_by || !muted_user) return -1;
    if (ensure_block_db() != 0) return -1;
    const char *sql = "INSERT OR REPLACE INTO mutes (room_name, muted_by, muted_user, created_at) VALUES (?, ?, ?, strftime('%s','now'));";
    pthread_mutex_lock(&block_lock);
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(block_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&block_lock);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, muted_by, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, muted_user, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&block_lock);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int mute_remove(const char *room_name, const char *muted_user)
{
    if (!room_name || !muted_user) return -1;
    if (ensure_block_db() != 0) return -1;
    const char *sql = "DELETE FROM mutes WHERE room_name = ? AND muted_user = ?;";
    pthread_mutex_lock(&block_lock);
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(block_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&block_lock);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, muted_user, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(block_db);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&block_lock);
    return (rc == SQLITE_DONE && changes > 0) ? 0 : -1;
}

int mute_check(const char *room_name, const char *muted_user)
{
    if (!room_name || !muted_user) return 0;
    if (ensure_block_db() != 0) return 0;
    const char *sql = "SELECT 1 FROM mutes WHERE room_name = ? AND muted_user = ? LIMIT 1;";
    pthread_mutex_lock(&block_lock);
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(block_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&block_lock);
        return 0;
    }
    sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, muted_user, -1, SQLITE_STATIC);
    int found = (sqlite3_step(stmt) == SQLITE_ROW) ? 1 : 0;
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&block_lock);
    return found;
}

int ban_add(const char *room_name, const char *banned_by, const char *banned_user)
{
    if (!room_name || !banned_by || !banned_user) return -1;
    if (ensure_block_db() != 0) return -1;
    const char *sql = "INSERT OR REPLACE INTO bans (room_name, banned_by, banned_user, created_at) VALUES (?, ?, ?, strftime('%s','now'));";
    pthread_mutex_lock(&block_lock);
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(block_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&block_lock);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, banned_by, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, banned_user, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&block_lock);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int ban_remove(const char *room_name, const char *banned_user)
{
    if (!room_name || !banned_user) return -1;
    if (ensure_block_db() != 0) return -1;
    const char *sql = "DELETE FROM bans WHERE room_name = ? AND banned_user = ?;";
    pthread_mutex_lock(&block_lock);
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(block_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&block_lock);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, banned_user, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(block_db);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&block_lock);
    return (rc == SQLITE_DONE && changes > 0) ? 0 : -1;
}

int ban_check(const char *room_name, const char *banned_user)
{
    if (!room_name || !banned_user) return 0;
    if (ensure_block_db() != 0) return 0;
    const char *sql = "SELECT 1 FROM bans WHERE room_name = ? AND banned_user = ? LIMIT 1;";
    pthread_mutex_lock(&block_lock);
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(block_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&block_lock);
        return 0;
    }
    sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, banned_user, -1, SQLITE_STATIC);
    int found = (sqlite3_step(stmt) == SQLITE_ROW) ? 1 : 0;
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&block_lock);
    return found;
}
