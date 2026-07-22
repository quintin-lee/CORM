#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef CORM_HAVE_SQLITE3
#include <sqlite3.h>
#endif

#include "corm_pub.h"

#include "../internal/corm_internal.h"

/* ── SQLite backend ── */

#ifdef CORM_HAVE_SQLITE3

static corm_err_t sqlite_open(corm_t *db, const char *dsn) {
    sqlite3 *handle;
    int rc = sqlite3_open(dsn, &handle);
    if (rc != SQLITE_OK) {
        snprintf(db->err_msg, sizeof(db->err_msg), "%s", sqlite3_errmsg(handle));
        sqlite3_close(handle);
        return CORM_ERR_BACKEND;
    }
    /* Enable WAL mode for better concurrency */
    sqlite3_exec(handle, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    /* Enable foreign keys */
    sqlite3_exec(handle, "PRAGMA foreign_keys=ON", NULL, NULL, NULL);
    db->conn = handle;
    return CORM_OK;
}

static corm_err_t sqlite_close(corm_t *db) {
    sqlite3 *handle = (sqlite3 *)db->conn;
    if (handle) {
        sqlite3_close(handle);
        db->conn = NULL;
    }
    return CORM_OK;
}

static corm_err_t sqlite_ping(corm_t *db) {
    sqlite3 *handle = (sqlite3 *)db->conn;
    if (!handle) return CORM_ERR_BACKEND;
    sqlite3_exec(handle, "SELECT 1", NULL, NULL, NULL);
    return CORM_OK;
}

static corm_err_t sqlite_exec(corm_t *db, const char *sql,
                              corm_value_t *params, int param_count) {
    sqlite3 *handle = (sqlite3 *)db->conn;
    sqlite3_stmt *stmt;
    (void)params;
    (void)param_count;

    const char *tail;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, &tail);
    if (rc != SQLITE_OK) {
        snprintf(db->err_msg, sizeof(db->err_msg), "%s", sqlite3_errmsg(handle));
        return CORM_ERR_BACKEND;
    }

    /* Bind params */
    for (int i = 0; i < param_count; i++) {
        corm_value_t *val = &params[i];
        int idx = i + 1;
        if (val->is_null) {
            sqlite3_bind_null(stmt, idx);
        } else {
            switch (val->type) {
                case CORM_INT:
                case CORM_INT64:
                    sqlite3_bind_int64(stmt, idx, val->v.i);
                    break;
                case CORM_FLOAT:
                case CORM_DOUBLE:
                    sqlite3_bind_double(stmt, idx, val->v.f);
                    break;
                case CORM_STRING:
                case CORM_TEXT:
                    sqlite3_bind_text(stmt, idx, val->v.s, -1, SQLITE_TRANSIENT);
                    break;
                case CORM_BOOL:
                    sqlite3_bind_int(stmt, idx, val->v.b ? 1 : 0);
                    break;
                case CORM_BLOB:
                    sqlite3_bind_blob(stmt, idx, val->v.blob.data,
                                      (int)val->v.blob.len, SQLITE_TRANSIENT);
                    break;
            }
        }
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        snprintf(db->err_msg, sizeof(db->err_msg), "%s", sqlite3_errmsg(handle));
        return CORM_ERR_BACKEND;
    }
    return CORM_OK;
}

static corm_err_t sqlite_query(corm_t *db, const char *sql,
                               corm_value_t *params, int param_count,
                               corm_result_t **out) {
    sqlite3 *handle = (sqlite3 *)db->conn;
    sqlite3_stmt *stmt;
    (void)params;
    (void)param_count;

    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(db->err_msg, sizeof(db->err_msg), "%s", sqlite3_errmsg(handle));
        return CORM_ERR_BACKEND;
    }

    /* Bind params */
    for (int i = 0; i < param_count; i++) {
        corm_value_t *val = &params[i];
        int idx = i + 1;
        if (val->is_null) {
            sqlite3_bind_null(stmt, idx);
        } else {
            switch (val->type) {
                case CORM_INT:
                case CORM_INT64:
                    sqlite3_bind_int64(stmt, idx, val->v.i);
                    break;
                case CORM_FLOAT:
                case CORM_DOUBLE:
                    sqlite3_bind_double(stmt, idx, val->v.f);
                    break;
                case CORM_STRING:
                case CORM_TEXT:
                    sqlite3_bind_text(stmt, idx, val->v.s, -1, SQLITE_TRANSIENT);
                    break;
                case CORM_BOOL:
                    sqlite3_bind_int(stmt, idx, val->v.b ? 1 : 0);
                    break;
                case CORM_BLOB:
                    sqlite3_bind_blob(stmt, idx, val->v.blob.data,
                                      (int)val->v.blob.len, SQLITE_TRANSIENT);
                    break;
            }
        }
    }

    /* Count rows first */
    int row_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        row_count++;
    }
    sqlite3_reset(stmt);

    int col_count = sqlite3_column_count(stmt);
    corm_result_t *res = corm_result_new(col_count, row_count);
    if (!res) {
        sqlite3_finalize(stmt);
        return CORM_ERR_NOMEM;
    }

    for (int i = 0; i < col_count; i++) {
        res->column_names[i] = strdup(sqlite3_column_name(stmt, i));
        res->column_types[i] = CORM_TEXT;
    }

    row_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        for (int i = 0; i < col_count; i++) {
            corm_value_t *v = &res->rows[row_count][i];
            int stype = sqlite3_column_type(stmt, i);
            v->is_null = (stype == SQLITE_NULL);
            if (v->is_null) continue;
            if (row_count == 0) {
                switch (stype) {
                    case SQLITE_INTEGER: res->column_types[i] = CORM_INT64; break;
                    case SQLITE_FLOAT:   res->column_types[i] = CORM_DOUBLE; break;
                    case SQLITE_BLOB:    res->column_types[i] = CORM_BLOB; break;
                    default:             res->column_types[i] = CORM_TEXT; break;
                }
            }
            switch (res->column_types[i]) {
                case CORM_INT64:
                    v->v.i = sqlite3_column_int64(stmt, i);
                    v->type = CORM_INT64;
                    break;
                case CORM_DOUBLE:
                    v->v.f = sqlite3_column_double(stmt, i);
                    v->type = CORM_DOUBLE;
                    break;
                case CORM_TEXT: {
                    const char *txt = (const char *)sqlite3_column_text(stmt, i);
                    v->v.s = txt ? strdup(txt) : NULL;
                    v->type = CORM_TEXT;
                    break;
                }
                case CORM_BLOB: {
                    int n = sqlite3_column_bytes(stmt, i);
                    v->v.blob.data = malloc((size_t)n);
                    if (v->v.blob.data) {
                        memcpy(v->v.blob.data, sqlite3_column_blob(stmt, i), (size_t)n);
                        v->v.blob.len = (size_t)n;
                    }
                    v->type = CORM_BLOB;
                    break;
                }
                default: break;
            }
        }
        row_count++;
    }

    sqlite3_finalize(stmt);
    *out = res;
    return CORM_OK;
}

static corm_err_t sqlite_begin(corm_t *db) {
    return sqlite_exec(db, "BEGIN TRANSACTION", NULL, 0);
}

static corm_err_t sqlite_commit(corm_t *db) {
    return sqlite_exec(db, "COMMIT", NULL, 0);
}

static corm_err_t sqlite_rollback(corm_t *db) {
    return sqlite_exec(db, "ROLLBACK", NULL, 0);
}

static size_t sqlite_escape(corm_t *db, char *dst, const char *src, size_t len) {
    (void)db;
    /* SQLite: double single quotes */
    size_t j = 0;
    for (size_t i = 0; i < len && src[i]; i++) {
        if (src[i] == '\'') {
            if (dst) dst[j++] = '\'';
        }
        if (dst) dst[j++] = src[i];
    }
    if (dst) dst[j] = '\0';
    return j;
}

static int64_t sqlite_last_id(corm_t *db) {
    sqlite3 *handle = (sqlite3 *)db->conn;
    return (int64_t)sqlite3_last_insert_rowid(handle);
}

static int sqlite_affected(corm_t *db) {
    sqlite3 *handle = (sqlite3 *)db->conn;
    return sqlite3_changes(handle);
}

static corm_backend_t sqlite3_backend = {
    .name = "sqlite3",
    .type = CORM_BACKEND_SQLITE,
    .open = sqlite_open,
    .close = sqlite_close,
    .ping = sqlite_ping,
    .exec = sqlite_exec,
    .query = sqlite_query,
    .begin = sqlite_begin,
    .commit = sqlite_commit,
    .rollback = sqlite_rollback,
    .escape_string = sqlite_escape,
    .last_insert_id = sqlite_last_id,
    .rows_affected = sqlite_affected,
};

/* Constructor — auto-register on load */
__attribute__((constructor))
static void sqlite_register(void) {
    corm_register_backend(CORM_BACKEND_SQLITE, &sqlite3_backend);
}

corm_err_t corm_register_sqlite3_backend(void) {
    return corm_register_backend(CORM_BACKEND_SQLITE, &sqlite3_backend);
}

#else /* CORM_HAVE_SQLITE3 */

/* Stub — no SQLite support */
static corm_backend_t sqlite3_stub_backend = {
    .name = "sqlite3 (unavailable)",
    .type = CORM_BACKEND_SQLITE,
    .open = NULL,
    .close = NULL,
    .ping = NULL,
    .exec = NULL,
    .query = NULL,
    .begin = NULL,
    .commit = NULL,
    .rollback = NULL,
    .escape_string = NULL,
    .last_insert_id = NULL,
    .rows_affected = NULL,
};

__attribute__((constructor))
static void sqlite_stub_register(void) {
    corm_register_backend(CORM_BACKEND_SQLITE, &sqlite3_stub_backend);
}

corm_err_t corm_register_sqlite3_backend(void) {
    return corm_register_backend(CORM_BACKEND_SQLITE, &sqlite3_stub_backend);
}

#endif /* CORM_HAVE_SQLITE3 */
