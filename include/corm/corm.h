#ifndef CORM_H
#define CORM_H

#include "corm/backend.h"
#include "corm/config.h"
#include "corm/model.h"
#include "corm/pool.h"
#include "corm/query.h"
#include "corm/result.h"
#include "corm/types.h"

struct corm;
typedef struct corm corm_t;

/* ── Connection & Transaction API ── */
extern corm_err_t corm_open(const char *dsn, corm_t **out);
extern corm_err_t corm_open_with_config(const char *dsn, corm_config_t config,
                                        corm_t **out);
extern corm_err_t corm_close(corm_t *db);
extern corm_err_t corm_ping(corm_t *db);
extern void corm_set_logger(corm_t *db, corm_logger_fn logger, void *user_data);
extern corm_err_t corm_begin(corm_t *db);
extern corm_err_t corm_commit(corm_t *db);
extern corm_err_t corm_rollback(corm_t *db);
extern corm_err_t corm_savepoint(corm_t *db, const char *name);
extern corm_err_t corm_rollback_to(corm_t *db, const char *name);
extern corm_err_t corm_release_savepoint(corm_t *db, const char *name);
extern corm_err_t corm_exec(corm_t *db, const char *sql);
extern corm_err_t corm_raw(corm_t *db, const char *sql, corm_result_t **out);
extern const char *corm_err_string(corm_err_t err);
extern corm_err_t corm_last_error(corm_t *db, char *buf, size_t bufsz);
extern corm_err_t corm_auto_migrate(corm_t *db, corm_model_t *models[],
                                    int model_count);
extern corm_err_t corm_init(void);
extern corm_err_t corm_register_backend(corm_backend_type_t type,
                                        corm_backend_t *backend);
extern corm_backend_t *corm_get_backend(corm_backend_type_t type);
extern corm_err_t corm_register_model(corm_t *db, corm_model_t *model);
extern corm_model_t *corm_find_model(corm_t *db, const char *table_name);
extern corm_field_t *corm_find_field(corm_model_t *model,
                                     const char *field_name);
extern corm_value_t corm_field_get_value(void *record, corm_field_t *field);
extern void corm_field_set_value(void *record, corm_field_t *field,
                                 corm_value_t *val);
extern corm_err_t corm_find_all(corm_t *db, corm_model_t *model,
                                const char *where, void *records, int *count);
extern corm_err_t corm_create_one(corm_t *db, corm_model_t *model, void *record,
                                  int64_t *insert_id);
extern corm_err_t corm_create_batch(corm_t *db, corm_model_t *model,
                                    void *records, int count, int batch_size,
                                    int *inserted_count);

/* ── Dialect API ── */
extern const char *corm_dialect_if_not_exists(corm_backend_type_t backend);
extern const char *corm_dialect_quote(corm_backend_type_t backend,
                                      const char *name);
extern const char *corm_dialect_autoinc(corm_backend_type_t backend);
extern void corm_dialect_type_name_str(corm_backend_type_t backend,
                                       corm_field_type_t type, size_t size,
                                       char *buf, size_t bufsz);
extern void corm_dialect_placeholder_str(corm_backend_type_t backend, int index,
                                         char *buf, size_t bufsz);
extern corm_err_t corm_build_sql(corm_query_t *q, corm_strbuf_t *sql,
                                 corm_backend_type_t bt);

#endif /* CORM_H */
