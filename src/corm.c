#include "corm_pub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "internal/corm_internal.h"

corm_err_t corm_dsn_parse(const char *dsn, corm_backend_type_t *out_type,
                          const char **out_body) {
  if (!dsn || !out_type || !out_body)
    return CORM_ERR_NULL;

  if (strncmp(dsn, "sqlite3://", 10) == 0) {
    *out_type = CORM_BACKEND_SQLITE;
    *out_body = dsn + 10;
  } else if (strncmp(dsn, "sqlite://", 9) == 0) {
    *out_type = CORM_BACKEND_SQLITE;
    *out_body = dsn + 9;
  } else if (strncmp(dsn, "mysql://", 8) == 0) {
    *out_type = CORM_BACKEND_MYSQL;
    *out_body = dsn + 8;
  } else if (strncmp(dsn, "postgres://", 11) == 0) {
    *out_type = CORM_BACKEND_POSTGRES;
    *out_body = dsn + 11;
  } else if (strncmp(dsn, "postgresql://", 13) == 0) {
    *out_type = CORM_BACKEND_POSTGRES;
    *out_body = dsn + 13;
  } else {
    *out_type = CORM_BACKEND_SQLITE;
    *out_body = dsn;
  }
  return CORM_OK;
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

  /* Init statement cache (configurable size, default 64) */
  db->stmt_cache = NULL;
  size_t cache_size =
      (size_t)(db->config.stmt_cache_size > 0 ? db->config.stmt_cache_size
                                              : 64);
  corm_stmt_cache_create(cache_size, &db->stmt_cache);

  /* Parse DSN and get backend */
  const char *dsn_body;
  corm_backend_type_t backend_type;
  err = corm_dsn_parse(dsn, &backend_type, &dsn_body);
  if (err) {
    corm_model_registry_free(&db->registry);
    free(db);
    return err;
  }

  db->backend = corm_get_backend(backend_type);
  if (!db->backend) {
    corm_set_err_msg(db, "Backend '%s' not found",
                     backend_type == CORM_BACKEND_MYSQL      ? "mysql"
                     : backend_type == CORM_BACKEND_POSTGRES ? "postgres"
                                                             : "sqlite3");
    corm_model_registry_free(&db->registry);
    free(db);
    return CORM_ERR_NOTFOUND;
  }

  if (!db->backend->open) {
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

  /* Record creation time for conn_max_lifetime_ms enforcement */
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  db->created_at_ms = (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;

  *out = db;
  return CORM_OK;
}

corm_err_t corm_close(corm_t *db) {
  if (!db)
    return CORM_ERR_NULL;
  corm_stmt_cache_destroy(db->stmt_cache);
  if (db->backend && db->conn) {
    db->backend->close(db);
  }
  db->stmt_cache = NULL;
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

corm_err_t corm_transaction(corm_t *db, corm_tx_fn fn, void *arg) {
  if (!db || !fn)
    return CORM_ERR_NULL;

  corm_err_t err = corm_begin(db);
  if (err != CORM_OK)
    return err;

  err = fn(db, arg);
  if (err == CORM_OK) {
    corm_err_t cerr = corm_commit(db);
    return cerr != CORM_OK ? cerr : CORM_OK;
  } else {
    corm_rollback(db);
    return err;
  }
}

static const char *isolation_level_sql(corm_isolation_level_t level) {
  switch (level) {
  case CORM_ISOLATION_READ_UNCOMMITTED:
    return "READ UNCOMMITTED";
  case CORM_ISOLATION_READ_COMMITTED:
    return "READ COMMITTED";
  case CORM_ISOLATION_REPEATABLE_READ:
    return "REPEATABLE READ";
  case CORM_ISOLATION_SERIALIZABLE:
    return "SERIALIZABLE";
  }
  return NULL;
}

corm_err_t corm_set_isolation(corm_t *db, corm_isolation_level_t level) {
  if (!db || !db->backend)
    return CORM_ERR_NULL;
  const char *sql = isolation_level_sql(level);
  if (!sql) {
    corm_set_err_msg(db, "Invalid isolation level: %d", level);
    return CORM_ERR_NULL;
  }
  char buf[256];
  snprintf(buf, sizeof(buf), "SET TRANSACTION ISOLATION LEVEL %s", sql);
  return corm_exec(db, buf);
}

static int is_valid_savepoint_name(const char *name) {
  if (!name || !*name)
    return 0;
  for (const char *p = name; *p; p++) {
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '_' || *p == '-'))
      return 0;
  }
  return 1;
}

corm_err_t corm_savepoint(corm_t *db, const char *name) {
  if (!db || !name)
    return CORM_ERR_NULL;
  if (!is_valid_savepoint_name(name)) {
    corm_set_err_msg(db, "Invalid savepoint name: '%s'", name);
    return CORM_ERR_NULL;
  }
  char sql[256];
  snprintf(sql, sizeof(sql), "SAVEPOINT %s", name);
  return corm_exec(db, sql);
}

corm_err_t corm_rollback_to(corm_t *db, const char *name) {
  if (!db || !name)
    return CORM_ERR_NULL;
  if (!is_valid_savepoint_name(name)) {
    corm_set_err_msg(db, "Invalid savepoint name: '%s'", name);
    return CORM_ERR_NULL;
  }
  char sql[256];
  snprintf(sql, sizeof(sql), "ROLLBACK TO SAVEPOINT %s", name);
  return corm_exec(db, sql);
}

corm_err_t corm_release_savepoint(corm_t *db, const char *name) {
  if (!db || !name)
    return CORM_ERR_NULL;
  if (!is_valid_savepoint_name(name)) {
    corm_set_err_msg(db, "Invalid savepoint name: '%s'", name);
    return CORM_ERR_NULL;
  }
  char sql[256];
  snprintf(sql, sizeof(sql), "RELEASE SAVEPOINT %s", name);
  return corm_exec(db, sql);
}

/* ── Raw SQL ── */

void corm_set_logger(corm_t *db, corm_logger_fn logger, void *user_data) {
  if (!db)
    return;
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
  uint64_t elapsed_us = (uint64_t)(end.tv_sec - start.tv_sec) * 1000000 +
                        (uint64_t)(end.tv_nsec - start.tv_nsec) / 1000;

  db->last_err = err;
  if (err) {
    corm_set_err_sql(db, sql);
  }

  if (db->logger) {
    corm_log_level_t lvl = err ? CORM_LOG_ERROR : CORM_LOG_INFO;
    db->logger(lvl, sql, elapsed_us, db->logger_user_data);
  }

  return err;
}

corm_err_t corm_exec_params(corm_t *db, const char *sql, corm_value_t *params,
                            int param_count) {
  if (!db || !db->backend)
    return CORM_ERR_NULL;

  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  corm_err_t err = db->backend->exec(db, sql, params, param_count);

  clock_gettime(CLOCK_MONOTONIC, &end);
  uint64_t elapsed_us = (uint64_t)(end.tv_sec - start.tv_sec) * 1000000 +
                        (uint64_t)(end.tv_nsec - start.tv_nsec) / 1000;

  db->last_err = err;
  if (err) {
    corm_set_err_sql(db, sql);
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
  uint64_t elapsed_us = (uint64_t)(end.tv_sec - start.tv_sec) * 1000000 +
                        (uint64_t)(end.tv_nsec - start.tv_nsec) / 1000;

  db->last_err = err;
  if (err) {
    corm_set_err_sql(db, sql);
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
