#ifndef PG_H
#define PG_H

typedef struct pg_config {
    char dsn[512];
    int  enabled;
} pg_config_t;

int  pg_init(const pg_config_t *cfg);
void pg_close(void);
int  pg_is_connected(void);

int  pg_exec(const char *sql);
int  pg_prepare(const char *name, const char *sql, int n_params);
int  pg_exec_prepared(const char *name, int n_params, const char **params);

#endif
