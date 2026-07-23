#include "corm_pub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/corm_internal.h"

/* ── DSN parsing (minimal) ── */

static corm_backend_type_t parse_backend(const char *dsn,
                                         const char **out_start) {
  if (strncmp(dsn, "sqlite3://", 10) == 0) {
    *out_start = dsn + 10;
    return CORM_BACKEND_SQLITE;
  }
  if (strncmp(dsn, "sqlite://", 9) == 0) {
    *out_start = dsn + 9;
    return CORM_BACKEND_SQLITE;
  }
  if (strncmp(dsn, "mysql://", 8) == 0) {
    *out_start = dsn + 8;
    return CORM_BACKEND_MYSQL;
  }
  if (strncmp(dsn, "postgres://", 11) == 0 ||
      strncmp(dsn, "postgresql://", 13) == 0) {
    *out_start = dsn + (dsn[8] == 's' ? 11 : 13);
    return CORM_BACKEND_POSTGRES;
  }
  *out_start = dsn;
  return CORM_BACKEND_SQLITE; /* default */
}

/* ── Core API ── */

corm_err_t corm_open(const char *dsn, corm_t **out) {
  corm_config_t config = CORM_DEFAULT_CONFIG;
  return corm_open_with_config(dsn, config, out);
}

corm_err_t corm_open_with_config(const char *dsn, corm_config_t config,
                                 corm_t **out) {
  if (!dsn || !out)
    return CORM_ERR_NULL;

  corm_t *db = (corm_t *)calloc(1, sizeof(corm_t));
  if (!db)
    return CORM_ERR_NOMEM;

  db->config = config;
  db->last_err = CORM_OK;
  db->err_msg[0] = '\0';
  db->err_sql[0] = '\0';

  /* Init model registry */
  corm_err_t err = corm_model_registry_init(&db->registry);
  if (err) {
    free(db);
    return err;
  }

  /* Parse DSN and get backend */
  const char *dsn_body;
  corm_backend_type_t backend_type = parse_backend(dsn, &dsn_body);

  db->backend = corm_get_backend(backend_type);
  if (!db->backend) {
    corm_model_registry_free(&db->registry);
    free(db);
    return CORM_ERR_NOTFOUND;
  }

  if (!db->backend->open) {
    snprintf(db->err_msg, sizeof(db->err_msg),
             "Backend '%s' is not available (library not found)",
             db->backend->name);
    corm_model_registry_free(&db->registry);
    free(db);
    return CORM_ERR_BACKEND;
  }

  /* Open connection */
  err = db->backend->open(db, dsn_body);
  if (err) {
    corm_model_registry_free(&db->registry);
    free(db);
    return err;
  }

  *out = db;
  return CORM_OK;
}

corm_err_t corm_close(corm_t *db) {
  if (!db)
    return CORM_ERR_NULL;
  if (db->backend && db->conn) {
    db->backend->close(db);
  }
  corm_model_registry_free(&db->registry);
  free(db);
  return CORM_OK;
}

corm_err_t corm_ping(corm_t *db) {
  if (!db || !db->backend)
    return CORM_ERR_NULL;
  return db->backend->ping(db);
}

/* ── Transaction API ── */

corm_err_t corm_begin(corm_t *db) {
  if (!db || !db->backend)
    return CORM_ERR_NULL;
  return db->backend->begin(db);
}

corm_err_t corm_commit(corm_t *db) {
  if (!db || !db->backend)
    return CORM_ERR_NULL;
  return db->backend->commit(db);
}

corm_err_t corm_rollback(corm_t *db) {
  if (!db || !db->backend)
    return CORM_ERR_NULL;
  return db->backend->rollback(db);
}

corm_err_t corm_savepoint(corm_t *db, const char *name) {
  if (!db || !name)
    return CORM_ERR_NULL;
  char sql[256];
  snprintf(sql, sizeof(sql), "SAVEPOINT %s", name);
  return corm_exec(db, sql);
}

corm_err_t corm_rollback_to(corm_t *db, const char *name) {
  if (!db || !name)
    return CORM_ERR_NULL;
  char sql[256];
  snprintf(sql, sizeof(sql), "ROLLBACK TO SAVEPOINT %s", name);
  return corm_exec(db, sql);
}

corm_err_t corm_release_savepoint(corm_t *db, const char *name) {
  if (!db || !name)
    return CORM_ERR_NULL;
  char sql[256];
  snprintf(sql, sizeof(sql), "RELEASE SAVEPOINT %s", name);
  return corm_exec(db, sql);
}

/* ── Raw SQL ── */

#include <time.h>

void corm_set_logger(corm_t *db, corm_logger_fn logger, void *user_data) {
  if (!db) return;
  db->logger = logger;
  db->logger_user_data = user_data;
}

corm_err_t corm_exec(corm_t *db, const char *sql) {
  if (!db || !db->backend)
    return CORM_ERR_NULL;

  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  corm_err_t err = db->backend->exec(db, sql, NULL, 0);

  clock_gettime(CLOCK_MONOTONIC, &end);
  uint64_t elapsed_us = (uint64_t)(end.tv_sec - start.tv_sec) * 1000000 + (uint64_t)(end.tv_nsec - start.tv_nsec) / 1000;

  db->last_err = err;
  if (err) {
    strncpy(db->err_sql, sql, sizeof(db->err_sql) - 1);
  }

  if (db->logger) {
    corm_log_level_t lvl = err ? CORM_LOG_ERROR : CORM_LOG_INFO;
    db->logger(lvl, sql, elapsed_us, db->logger_user_data);
  }

  return err;
}

corm_err_t corm_raw(corm_t *db, const char *sql, corm_result_t **out) {
  if (!db || !db->backend)
    return CORM_ERR_NULL;

  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  corm_err_t err = db->backend->query(db, sql, NULL, 0, out);

  clock_gettime(CLOCK_MONOTONIC, &end);
  uint64_t elapsed_us = (uint64_t)(end.tv_sec - start.tv_sec) * 1000000 + (uint64_t)(end.tv_nsec - start.tv_nsec) / 1000;

  db->last_err = err;
  if (err) {
    strncpy(db->err_sql, sql, sizeof(db->err_sql) - 1);
  }

  if (db->logger) {
    corm_log_level_t lvl = err ? CORM_LOG_ERROR : CORM_LOG_INFO;
    db->logger(lvl, sql, elapsed_us, db->logger_user_data);
  }

  return err;
}

/* ── Utility ── */

const char *corm_err_string(corm_err_t err) {
  switch (err) {
  case CORM_OK:
    return "OK";
  case CORM_ERR_GENERIC:
    return "Generic error";
  case CORM_ERR_NOMEM:
    return "Out of memory";
  case CORM_ERR_NOTFOUND:
    return "Not found";
  case CORM_ERR_DUP:
    return "Duplicate entry";
  case CORM_ERR_BACKEND:
    return "Backend error";
  case CORM_ERR_TYPE:
    return "Type mismatch";
  case CORM_ERR_NULL:
    return "Null pointer";
  case CORM_ERR_BOUNDS:
    return "Out of bounds";
  case CORM_ERR_MISMATCH:
    return "Mismatch";
  default:
    return "Unknown error";
  }
}

corm_err_t corm_last_error(corm_t *db, char *buf, size_t bufsz) {
  if (!db || !buf)
    return CORM_ERR_NULL;
  snprintf(buf, bufsz, "%s: %s (SQL: %s)", corm_err_string(db->last_err),
           db->err_msg[0] ? db->err_msg : "no details",
           db->err_sql[0] ? db->err_sql : "none");
  return db->last_err;
}

/* ── Backend initialization ── */

extern corm_err_t corm_register_sqlite3_backend(void);
extern corm_err_t corm_register_mysql_backend(void);
extern corm_err_t corm_register_postgres_backend(void);

corm_err_t corm_init(void) {
  corm_err_t err;
  err = corm_register_sqlite3_backend();
  if (err)
    return err;
  err = corm_register_mysql_backend();
  if (err)
    return err;
  err = corm_register_postgres_backend();
  if (err)
    return err;
  return CORM_OK;
}
