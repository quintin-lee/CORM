#ifndef CORM_BACKEND_H
#define CORM_BACKEND_H

#include "types.h"

struct corm;
struct corm_result;

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
} corm_backend_t;

#endif /* CORM_BACKEND_H */
