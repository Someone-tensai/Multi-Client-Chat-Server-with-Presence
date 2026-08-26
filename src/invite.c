#include "../include/invite.h"
#include "../include/db.h"
#include "../include/log.h"
#include <sqlite3.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

static sqlite3 *invite_db = NULL;
static pthread_mutex_t invite_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *INVITE_SCHEMA =
    "CREATE TABLE IF NOT EXISTS invites ("
    "    room_name TEXT NOT NULL,"
    "    inviter   TEXT NOT NULL,"
    "    invitee   TEXT NOT NULL,"
    "    created_at INTEGER NOT NULL,"
    "    PRIMARY KEY (room_name, invitee)"
    ");"
    "CREATE TABLE IF NOT EXISTS private_rooms ("
    "    room_name TEXT PRIMARY KEY,"
    "    owner     TEXT NOT NULL,"
    "    created_at INTEGER NOT NULL"
    ");";

static int ensure_invite_db(void)
{
    if (invite_db) return 0;
    pthread_mutex_lock(&invite_lock);
    if (invite_db) { pthread_mutex_unlock(&invite_lock); return 0; }
    if (sqlite3_open(DB_FILE, &invite_db) != SQLITE_OK) {
        LOG_ERROR("invite_db: failed to open %s", DB_FILE);
        pthread_mutex_unlock(&invite_lock);
        return -1;
    }
    char *err = NULL;
    if (sqlite3_exec(invite_db, INVITE_SCHEMA, NULL, NULL, &err) != SQLITE_OK) {
        LOG_ERROR("invite_db schema: %s", err);
        sqlite3_free(err);
        sqlite3_close(invite_db);
        invite_db = NULL;
        pthread_mutex_unlock(&invite_lock);
        return -1;
    }
    pthread_mutex_unlock(&invite_lock);
    LOG_INFO("Invite database initialized");
    return 0;
}

int invite_init(void) { return ensure_invite_db(); }

int invite_create(const char *room_name, const char *inviter, const char *invitee)
{
    if (!room_name || !inviter || !invitee) return -1;
    if (ensure_invite_db() != 0) return -1;
    const char *sql = "INSERT OR IGNORE INTO invites (room_name, inviter, invitee, created_at) VALUES (?, ?, ?, strftime('%s','now'));";
    pthread_mutex_lock(&invite_lock);
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(invite_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&invite_lock);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, inviter, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, invitee, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&invite_lock);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int invite_accept(const char *room_name, const char *invitee)
{
    if (!room_name || !invitee) return -1;
    if (ensure_invite_db() != 0) return -1;
    const char *sql = "DELETE FROM invites WHERE room_name = ? AND invitee = ?;";
    pthread_mutex_lock(&invite_lock);
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(invite_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&invite_lock);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, invitee, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(invite_db);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&invite_lock);
    return (rc == SQLITE_DONE && changes > 0) ? 0 : -1;
}

int invite_decline(const char *room_name, const char *invitee)
{
    return invite_accept(room_name, invitee);
}

int invite_find(const char *room_name, const char *invitee)
{
    if (!room_name || !invitee) return 0;
    if (ensure_invite_db() != 0) return 0;
    const char *sql = "SELECT 1 FROM invites WHERE room_name = ? AND invitee = ? LIMIT 1;";
    pthread_mutex_lock(&invite_lock);
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(invite_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&invite_lock);
        return 0;
    }
    sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, invitee, -1, SQLITE_STATIC);
    int found = (sqlite3_step(stmt) == SQLITE_ROW) ? 1 : 0;
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&invite_lock);
    return found;
}

int invite_remove(const char *room_name, const char *invitee)
{
    return invite_accept(room_name, invitee);
}
