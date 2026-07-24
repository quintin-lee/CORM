#ifndef CORM_H
#define CORM_H

#include "corm/backend.h"
#include "corm/config.h"
#include "corm/model.h"
#include "corm/pool.h"
#include "corm/query.h"
#include "corm/result.h"
#include "corm/types.h"

/** Opaque database connection handle. All operations go through this. */
struct corm;
typedef struct corm corm_t;

/* ── Connection & Transaction API ── */

/** Open a database connection.
 *  DSN format: "sqlite3:///path/to/db" or "sqlite3://:memory:". */
extern corm_err_t corm_open(const char *dsn, corm_t **out);

/** Open a connection with custom config (pool limits, timeouts, logging). */
extern corm_err_t corm_open_with_config(const char *dsn, corm_config_t config,
                                        corm_t **out);

/** Close a database connection and free all resources. */
extern corm_err_t corm_close(corm_t *db);

/** Ping the database to verify connectivity. */
extern corm_err_t corm_ping(corm_t *db);

/** Set a logging callback for SQL queries. Pass NULL to disable. */
extern void corm_set_logger(corm_t *db, corm_logger_fn logger, void *user_data);

/** BEGIN a transaction. */
extern corm_err_t corm_begin(corm_t *db);

/** COMMIT the current transaction. */
extern corm_err_t corm_commit(corm_t *db);

/** ROLLBACK the current transaction. */
extern corm_err_t corm_rollback(corm_t *db);

/** Create a named savepoint. Name must be alphanumeric (underscore/hyphen
 * allowed only). */
extern corm_err_t corm_savepoint(corm_t *db, const char *name);

/** Roll back to a named savepoint without releasing it. */
extern corm_err_t corm_rollback_to(corm_t *db, const char *name);

/** Release (forget) a named savepoint. */
extern corm_err_t corm_release_savepoint(corm_t *db, const char *name);

/** Execute a raw SQL string (no binding). */
extern corm_err_t corm_exec(corm_t *db, const char *sql);

/** Execute a raw SQL query and return a result set. */
extern corm_err_t corm_raw(corm_t *db, const char *sql, corm_result_t **out);

/** Return a static error string for an error code. */
extern const char *corm_err_string(corm_err_t err);

/** Copy the last error message from the connection into buf. */
extern corm_err_t corm_last_error(corm_t *db, char *buf, size_t bufsz);

/** Auto-migrate: create missing tables and add missing columns for the given
 * models. */
extern corm_err_t corm_auto_migrate(corm_t *db, corm_model_t *models[],
                                    int model_count);

/** Initialize the library (currently a no-op; may be required in the future).
 */
extern corm_err_t corm_init(void);

/** Register a backend implementation for a specific database type. */
extern corm_err_t corm_register_backend(corm_backend_type_t type,
                                        corm_backend_t *backend);

/** Look up a registered backend by type. Returns NULL if not found. */
extern corm_backend_t *corm_get_backend(corm_backend_type_t type);

/** Register a model on the connection for auto-migration and lookup. */
extern corm_err_t corm_register_model(corm_t *db, corm_model_t *model);

/** Find a registered model by table name. Returns NULL if not found. */
extern corm_model_t *corm_find_model(corm_t *db, const char *table_name);

/** Find a field descriptor by name within a model. Returns NULL if not found.
 */
extern corm_field_t *corm_find_field(corm_model_t *model,
                                     const char *field_name);

/** Extract a corm_value_t from a record struct field (zero-copy — pointer into
 * struct). */
extern corm_value_t corm_field_get_value(void *record, corm_field_t *field);

/** Write a corm_value_t into a record struct field. */
extern void corm_field_set_value(void *record, corm_field_t *field,
                                 corm_value_t *val);

/* ── High-Level CRUD Conveniences ── */

/** Find all records matching a WHERE clause. Populates an array of structs. */
extern corm_err_t corm_find_all(corm_t *db, corm_model_t *model,
                                const char *where, void *records, int *count);

/** Find one record by WHERE condition. Populates a single struct. */
extern corm_err_t corm_find_one(corm_t *db, corm_model_t *model,
                                const char *where, void *record);

/** Count records matching a WHERE condition. Pass NULL for total count. */
extern corm_err_t corm_count(corm_t *db, corm_model_t *model, const char *where,
                             int *count);

/** Create a single record and return its insert ID. */
extern corm_err_t corm_create_one(corm_t *db, corm_model_t *model, void *record,
                                  int64_t *insert_id);

/** Create multiple records in batched transactions. */
extern corm_err_t corm_create_batch(corm_t *db, corm_model_t *model,
                                    void *records, int count, int batch_size,
                                    int *inserted_count);

/** Update multiple records identified by primary key in a transaction. */
extern corm_err_t corm_update_batch(corm_t *db, corm_model_t *model,
                                    void *records, int count,
                                    int *affected_count);

/** Delete multiple records identified by primary key in a transaction.
 *  Respects CORM_FLAG_SOFT_DELETE (performs UPDATE instead of DELETE). */
extern corm_err_t corm_delete_batch(corm_t *db, corm_model_t *model,
                                    void *records, int count,
                                    int *affected_count);

/* ── Dialect API ── */

/** Return "IF NOT EXISTS" clause for CREATE TABLE (empty string for MySQL). */
extern const char *corm_dialect_if_not_exists(corm_backend_type_t backend);
/** Return the dialect-specific identifier quote character as a string. */
extern const char *corm_dialect_quote(corm_backend_type_t backend,
                                      const char *name);
/** Return the dialect-specific AUTO_INCREMENT clause (empty for non-MySQL). */
extern const char *corm_dialect_autoinc(corm_backend_type_t backend);
/** Write the column type name for a given field type into buf. */
extern void corm_dialect_type_name_str(corm_backend_type_t backend,
                                       corm_field_type_t type, size_t size,
                                       char *buf, size_t bufsz);
/** Write the Nth placeholder string (? or $N) into buf. */
extern void corm_dialect_placeholder_str(corm_backend_type_t backend, int index,
                                         char *buf, size_t bufsz);
/** Build SQL from a query builder state. Used internally by exec/query. */
extern corm_err_t corm_build_sql(corm_query_t *q, corm_strbuf_t *sql,
                                 corm_backend_type_t bt);

#endif /* CORM_H */
