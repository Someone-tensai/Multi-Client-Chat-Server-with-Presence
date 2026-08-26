#include "../include/permission.h"
#include "../include/db.h"
#include "../include/log.h"
#include <sqlite3.h>
#include <string.h>
#include <pthread.h>

static sqlite3 *perm_db = NULL;
static pthread_mutex_t perm_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *PERM_SCHEMA =
    "CREATE TABLE IF NOT EXISTS room_roles ("
    "    room_name TEXT NOT NULL,"
    "    username  TEXT NOT NULL,"
    "    role      INTEGER NOT NULL DEFAULT 0,"
    "    PRIMARY KEY (room_name, username)"
    ");";

static int ensure_perm_db(void)
{
    if (perm_db) return 0;
    pthread_mutex_lock(&perm_lock);
    if (perm_db) { pthread_mutex_unlock(&perm_lock); return 0; }
    if (sqlite3_open(DB_FILE, &perm_db) != SQLITE_OK) {
        LOG_ERROR("perm_db: failed to open %s", DB_FILE);
        pthread_mutex_unlock(&perm_lock);
        return -1;
    }
    char *err = NULL;
    if (sqlite3_exec(perm_db, PERM_SCHEMA, NULL, NULL, &err) != SQLITE_OK) {
        LOG_ERROR("perm_db schema: %s", err);
        sqlite3_free(err);
        sqlite3_close(perm_db);
        perm_db = NULL;
        pthread_mutex_unlock(&perm_lock);
        return -1;
    }
    pthread_mutex_unlock(&perm_lock);
    LOG_INFO("Permission database initialized");
    return 0;
}

int permission_init(void) { return ensure_perm_db(); }

int permission_set_role(const char *room_name, const char *username, room_role_t role)
{
    if (!room_name || !username) return -1;
    if (ensure_perm_db() != 0) return -1;
    const char *sql = "INSERT OR REPLACE INTO room_roles (room_name, username, role) VALUES (?, ?, ?);";
    pthread_mutex_lock(&perm_lock);
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(perm_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&perm_lock);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, (int)role);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&perm_lock);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int permission_get_role(const char *room_name, const char *username, room_role_t *out)
{
    if (!room_name || !username || !out) return -1;
    if (ensure_perm_db() != 0) { *out = ROLE_MEMBER; return 0; }
    const char *sql = "SELECT role FROM room_roles WHERE room_name = ? AND username = ?;";
    pthread_mutex_lock(&perm_lock);
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(perm_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&perm_lock);
        *out = ROLE_MEMBER;
        return 0;
    }
    sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW)
        *out = (room_role_t)sqlite3_column_int(stmt, 0);
    else
        *out = ROLE_MEMBER;
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&perm_lock);
    return 0;
}

int permission_check(const char *room_name, const char *username, const char *action)
{
    room_role_t role = ROLE_MEMBER;
    permission_get_role(room_name, username, &role);

    if (strcmp(action, ACTION_MSG) == 0) return 1;

    if (strcmp(action, ACTION_INVITE) == 0)
        return (role >= ROLE_ADMIN) ? 1 : 0;

    if (strcmp(action, ACTION_KICK) == 0)
        return (role >= ROLE_ADMIN) ? 1 : 0;

    if (strcmp(action, ACTION_BAN) == 0)
        return (role >= ROLE_OWNER) ? 1 : 0;

    if (strcmp(action, ACTION_MUTE) == 0)
        return (role >= ROLE_ADMIN) ? 1 : 0;

    if (strcmp(action, ACTION_DEMOTE) == 0)
        return (role >= ROLE_OWNER) ? 1 : 0;

    return 0;
}

int permission_remove(const char *room_name, const char *username)
{
    if (!room_name || !username) return -1;
    if (ensure_perm_db() != 0) return -1;
    const char *sql = "DELETE FROM room_roles WHERE room_name = ? AND username = ?;";
    pthread_mutex_lock(&perm_lock);
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(perm_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&perm_lock);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, room_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&perm_lock);
    return 0;
}
