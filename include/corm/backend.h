#ifndef CORM_BACKEND_H
#define CORM_BACKEND_H

#include "types.h"
#include <stddef.h>

struct corm;
struct corm_result;

/** Column descriptor returned by the describe_table backend hook.
 *
 *  Populated by the backend via PRAGMA table_info (SQLite),
 *  DESCRIBE (MySQL), or information_schema.columns (PostgreSQL).
 *  Free with corm_column_info_free(). */
typedef struct corm_column_info {
  char *name;          /**< Column name (allocated, freed by _free) */
  char *type_name;     /**< Backend type string, e.g. "INTEGER" (allocated) */
  int not_null;        /**< 1 if NOT NULL, 0 otherwise */
  int is_pk;           /**< 1 if part of PRIMARY KEY, 0 otherwise */
  char *default_value; /**< Default expression or NULL if none (allocated) */
} corm_column_info_t;

/** Free an array of column info returned by describe_table. */
void corm_column_info_free(corm_column_info_t *cols, int count);

/** Backend virtual-table — one instance per supported database.
 *
 * Implementations: sqlite3, mysql, postgres. Registered at link time via
 * corm_register_backend() and dispatched through corm_open() DSN prefix. */
typedef struct corm_backend {
  const char *name;         /**< Human-readable backend name (e.g. "sqlite3") */
  corm_backend_type_t type; /**< Enum matching dialect/placeholder logic */
  corm_err_t (*open)(struct corm *db,
                     const char *dsn);  /**< Connect / open database */
  corm_err_t (*close)(struct corm *db); /**< Disconnect / close database */
  corm_err_t (*ping)(struct corm *db);  /**< Health-check the connection */
  corm_err_t (*exec)(struct corm *db, const char *sql, corm_value_t *params,
                     int param_count); /**< Execute write statement */
  corm_err_t (*query)(struct corm *db, const char *sql, corm_value_t *params,
                      int param_count,
                      struct corm_result **out); /**< Execute read query */
  corm_err_t (*begin)(struct corm *db);          /**< Begin transaction */
  corm_err_t (*commit)(struct corm *db);         /**< Commit transaction */
  corm_err_t (*rollback)(struct corm *db);       /**< Rollback transaction */
  size_t (*escape_string)(struct corm *db, char *dst, const char *src,
                          size_t len); /**< Escape string for SQL safety */
  int64_t (*last_insert_id)(
      struct corm *db); /**< Return last inserted row ID */
  int (*rows_affected)(
      struct corm *db); /**< Return rows affected by last exec */
  corm_err_t (*describe_table)(
      struct corm *db, const char *table_name, corm_column_info_t **out,
      int *count); /**< Introspect existing columns (allocates *out) */
} corm_backend_t;

#endif /* CORM_BACKEND_H */
