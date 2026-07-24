#ifndef CORM_INTERNAL_H
#define CORM_INTERNAL_H

#include "corm_pub.h"
#include "hash.h"
#include "stmt_cache.h"
#include <stdarg.h>
#include <stdio.h>

typedef struct {
  corm_hash_t models_by_table;
  corm_hash_t models_by_name;
} corm_registry_t;
corm_err_t corm_model_registry_init(corm_registry_t *reg);
void corm_model_registry_free(corm_registry_t *reg);

struct corm {
  corm_backend_t *backend;
  void *conn;
  corm_config_t config;
  corm_registry_t registry;
  corm_err_t last_err;
  char err_msg[512];
  char err_sql[1024];
  int64_t last_insert_id_val;
  int rows_affected_val;
  corm_logger_fn logger;
  void *logger_user_data;
  corm_stmt_cache_t *stmt_cache;
  int64_t created_at_ms; /**< Monotonic timestamp at open, for
                            conn_max_lifetime_ms */
};

/** Set db->err_msg with truncation marker ("...") if the formatted message
 * overflows the buffer. */
static inline void corm_set_err_msg(corm_t *db, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(db->err_msg, sizeof(db->err_msg), fmt, args);
  va_end(args);
  if ((size_t)n >= sizeof(db->err_msg))
    memcpy(db->err_msg + sizeof(db->err_msg) - 4, "...", 4);
}

/** Copy a SQL string into db->err_sql with truncation marker if it overflows.
 */
static inline void corm_set_err_sql(corm_t *db, const char *sql) {
  int n = snprintf(db->err_sql, sizeof(db->err_sql), "%s", sql);
  if ((size_t)n >= sizeof(db->err_sql))
    memcpy(db->err_sql + sizeof(db->err_sql) - 4, "...", 4);
}

/** Check if a field or table identifier contains only valid characters
 * ([a-zA-Z0-9_.*]). */
static inline bool corm_is_valid_identifier(const char *name) {
  if (!name || !*name)
    return false;
  for (const char *p = name; *p; p++) {
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '_' || *p == '.' || *p == '*'))
      return false;
  }
  return true;
}

#endif
