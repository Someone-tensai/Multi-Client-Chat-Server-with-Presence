#include "../include/pg.h"
#include "../include/log.h"
#include <string.h>

#ifdef USE_PG
#include <libpq-fe.h>
static PGconn *pg_conn = NULL;
#else
static void *pg_conn __attribute__((unused)) = NULL;
#endif

static int pg_enabled = 0;

int pg_init(const pg_config_t *cfg)
{
    if (!cfg || !cfg->enabled) {
        LOG_INFO("PostgreSQL disabled — using SQLite fallback");
        pg_enabled = 0;
        return 0;
    }

#ifdef USE_PG
    pg_conn = PQconnectdb(cfg->dsn);
    if (PQstatus(pg_conn) != CONNECTION_OK) {
        LOG_ERROR("PostgreSQL connection failed: %s", PQerrorMessage(pg_conn));
        PQfinish(pg_conn);
        pg_conn = NULL;
        pg_enabled = 0;
        return -1;
    }
    pg_enabled = 1;
    LOG_INFO("PostgreSQL connected");
    return 0;
#else
    (void)cfg;
    LOG_INFO("PostgreSQL support not compiled (install libpq + rebuild with USE_PG)");
    pg_enabled = 0;
    return 0;
#endif
}

void pg_close(void)
{
#ifdef USE_PG
    if (pg_conn) {
        PQfinish(pg_conn);
        pg_conn = NULL;
    }
#endif
    pg_enabled = 0;
}

int pg_is_connected(void) { return pg_enabled; }

int pg_exec(const char *sql)
{
    if (!pg_enabled || !sql) return -1;
#ifdef USE_PG
    PGresult *res = PQexec(pg_conn, sql);
    ExecStatusType st = PQresultStatus(res);
    PQclear(res);
    return (st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK) ? 0 : -1;
#else
    (void)sql;
    return -1;
#endif
}

int pg_prepare(const char *name, const char *sql, int n_params)
{
    if (!pg_enabled || !name || !sql) return -1;
#ifdef USE_PG
    PGresult *res = PQprepare(pg_conn, name, sql, n_params, NULL);
    ExecStatusType st = PQresultStatus(res);
    PQclear(res);
    return (st == PGRES_COMMAND_OK) ? 0 : -1;
#else
    (void)name; (void)sql; (void)n_params;
    return -1;
#endif
}

int pg_exec_prepared(const char *name, int n_params, const char **params)
{
    if (!pg_enabled || !name) return -1;
#ifdef USE_PG
    PGresult *res = PQexecPrepared(pg_conn, name, n_params, params, NULL, NULL, 0);
    ExecStatusType st = PQresultStatus(res);
    PQclear(res);
    return (st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK) ? 0 : -1;
#else
    (void)name; (void)n_params; (void)params;
    return -1;
#endif
}
