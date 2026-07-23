#ifndef CORM_BACKEND_H
#define CORM_BACKEND_H

#include "types.h"

struct corm;
struct corm_result;

typedef struct corm_backend {
  const char *name;
  corm_backend_type_t type;
  corm_err_t (*open)(struct corm *db, const char *dsn);
  corm_err_t (*close)(struct corm *db);
  corm_err_t (*ping)(struct corm *db);
  corm_err_t (*exec)(struct corm *db, const char *sql, corm_value_t *params,
                     int param_count);
  corm_err_t (*query)(struct corm *db, const char *sql, corm_value_t *params,
                      int param_count, struct corm_result **out);
  corm_err_t (*begin)(struct corm *db);
  corm_err_t (*commit)(struct corm *db);
  corm_err_t (*rollback)(struct corm *db);
  size_t (*escape_string)(struct corm *db, char *dst, const char *src,
                          size_t len);
  int64_t (*last_insert_id)(struct corm *db);
  int (*rows_affected)(struct corm *db);
} corm_backend_t;

#endif /* CORM_BACKEND_H */
