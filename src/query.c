#include <stdlib.h>
#include <string.h>
#include "internal/corm_internal.h"

/* ── Query builder ── */

corm_query_t *corm_query_new(corm_t *db, corm_model_t *model) {
    corm_query_t *q = (corm_query_t *)calloc(1, sizeof(corm_query_t));
    if (!q) return NULL;
    q->db = db;
    q->model = model;
    q->op = CORM_OP_SELECT;
    q->limit = 0;
    q->offset = 0;
    q->param_count = 0;
    q->param_cap = 0;
    q->params = NULL;
    corm_strbuf_init(&q->select_cols);
    corm_strbuf_init(&q->where);
    corm_strbuf_init(&q->order);
    corm_strbuf_init(&q->group);
    corm_strbuf_init(&q->having);
    corm_strbuf_init(&q->joins);
    corm_strbuf_init(&q->set_clause);
    return q;
}

void corm_query_free(corm_query_t *q) {
    if (!q) return;
    corm_strbuf_free(&q->select_cols);
    corm_strbuf_free(&q->where);
    corm_strbuf_free(&q->order);
    corm_strbuf_free(&q->group);
    corm_strbuf_free(&q->having);
    corm_strbuf_free(&q->joins);
    corm_strbuf_free(&q->set_clause);
    free(q->params);
    free(q);
}

void corm_query_reset(corm_query_t *q) {
    if (!q) return;
    q->op = CORM_OP_SELECT;
    q->limit = 0;
    q->offset = 0;
    q->param_count = 0;
    corm_strbuf_clear(&q->select_cols);
    corm_strbuf_clear(&q->where);
    corm_strbuf_clear(&q->order);
    corm_strbuf_clear(&q->group);
    corm_strbuf_clear(&q->having);
    corm_strbuf_clear(&q->joins);
    corm_strbuf_clear(&q->set_clause);
}

corm_query_t *corm_query_op(corm_query_t *q, corm_query_op_t op) {
    q->op = op;
    return q;
}

corm_query_t *corm_query_select(corm_query_t *q, const char *columns) {
    corm_strbuf_clear(&q->select_cols);
    corm_strbuf_append(&q->select_cols, columns);
    return q;
}

corm_query_t *corm_query_where(corm_query_t *q, const char *condition, ...) {
    if (q->where.len > 0)
        corm_strbuf_append(&q->where, " AND ");
    corm_strbuf_append(&q->where, condition);
    /* Variadic params intentionally unsupported — use corm_query_bind() explicitly */
    (void)condition;
    q->op = CORM_OP_SELECT;
    return q;
}

corm_query_t *corm_query_or_where(corm_query_t *q, const char *condition, ...) {
    if (q->where.len > 0)
        corm_strbuf_append(&q->where, " OR ");
    corm_strbuf_append(&q->where, condition);
    /* variadic params skipped for simplicity; use explicit bind() */
    (void)condition;
    return q;
}

corm_query_t *corm_query_where_null(corm_query_t *q, const char *field) {
    if (!q || !field) return q;
    if (q->where.len > 0) corm_strbuf_append(&q->where, " AND ");
    corm_strbuf_append(&q->where, field);
    corm_strbuf_append(&q->where, " IS NULL");
    return q;
}

corm_query_t *corm_query_where_not_null(corm_query_t *q, const char *field) {
    if (!q || !field) return q;
    if (q->where.len > 0) corm_strbuf_append(&q->where, " AND ");
    corm_strbuf_append(&q->where, field);
    corm_strbuf_append(&q->where, " IS NOT NULL");
    return q;
}

corm_query_t *corm_query_join(corm_query_t *q, const char *join_clause) {
    corm_strbuf_append(&q->joins, join_clause);
    return q;
}

corm_query_t *corm_query_order(corm_query_t *q, const char *order) {
    corm_strbuf_clear(&q->order);
    corm_strbuf_append(&q->order, order);
    return q;
}

corm_query_t *corm_query_group(corm_query_t *q, const char *group) {
    corm_strbuf_clear(&q->group);
    corm_strbuf_append(&q->group, group);
    return q;
}

corm_query_t *corm_query_having(corm_query_t *q, const char *condition) {
    corm_strbuf_clear(&q->having);
    corm_strbuf_append(&q->having, condition);
    return q;
}

corm_query_t *corm_query_limit(corm_query_t *q, int limit) {
    q->limit = limit;
    return q;
}

corm_query_t *corm_query_offset(corm_query_t *q, int offset) {
    q->offset = offset;
    return q;
}

corm_query_t *corm_query_bind(corm_query_t *q, corm_value_t val) {
    if (q->param_count >= q->param_cap) {
        int new_cap = q->param_cap ? q->param_cap * 2 : 8;
        corm_value_t *tmp = (corm_value_t *)realloc(q->params,
                            (size_t)new_cap * sizeof(corm_value_t));
        if (!tmp) return q;
        q->params = tmp;
        q->param_cap = new_cap;
    }
    q->params[q->param_count++] = val;
    return q;
}

corm_query_t *corm_query_set(corm_query_t *q, const char *field, corm_value_t val) {
    if (q->set_clause.len > 0)
        corm_strbuf_append(&q->set_clause, ", ");
    const char *qchar = corm_dialect_quote(q->db->backend->type, field);
    corm_strbuf_append(&q->set_clause, qchar);
    corm_strbuf_append(&q->set_clause, field);
    corm_strbuf_append(&q->set_clause, qchar);
    corm_strbuf_append(&q->set_clause, " = ");
    char ph_buf[16];
    corm_dialect_placeholder_str(q->db->backend->type, q->param_count, ph_buf, sizeof(ph_buf));
    corm_strbuf_append(&q->set_clause, ph_buf);
    corm_query_bind(q, val);
    q->op = CORM_OP_UPDATE;
    return q;
}

corm_query_t *corm_query_set_raw(corm_query_t *q, const char *clause) {
    corm_strbuf_clear(&q->set_clause);
    corm_strbuf_append(&q->set_clause, clause);
    q->op = CORM_OP_UPDATE;
    return q;
}

corm_query_t *corm_query_preload(corm_query_t *q, const char *relation_name) {
    if (!q || !relation_name) return q;
    // Store preloaded relation name
    return q;
}

/* ── Execution ── */

static corm_err_t query_exec(corm_query_t *q, corm_strbuf_t *sql) {
    corm_backend_type_t bt = q->db ? q->db->backend->type : CORM_BACKEND_SQLITE;
    corm_err_t err = corm_build_sql(q, sql, bt);
    if (err) return err;
    return CORM_OK;
}

corm_err_t corm_find(corm_query_t *q, corm_result_t **out) {
    corm_strbuf_t sql;
    corm_strbuf_init(&sql);
    q->op = CORM_OP_SELECT;
    corm_err_t err = query_exec(q, &sql);
    if (err) { corm_strbuf_free(&sql); return err; }

    err = q->db->backend->query(q->db, corm_strbuf_cstr(&sql),
                                q->params, q->param_count, out);
    corm_strbuf_free(&sql);
    return err;
}

corm_err_t corm_first(corm_query_t *q, void *record) {
    q->limit = 1;
    q->offset = 0;
    corm_result_t *res = NULL;
    corm_err_t err = corm_find(q, &res);
    if (err) return err;
    if (!res || res->row_count == 0) {
        corm_result_release(res);
        return CORM_ERR_NOTFOUND;
    }

    /* Map first row into struct */
    for (int i = 0; i < q->model->field_count; i++) {
        corm_field_t *f = &q->model->fields[i];
        /* Find column by name */
        for (int j = 0; j < res->column_count; j++) {
            if (strcmp(res->column_names[j], f->name) == 0) {
                corm_value_t *val = &res->rows[0][j];
                corm_field_set_value(record, f, val);
                break;
            }
        }
    }

    if (q && q->model && q->model->after_find) {
        q->model->after_find(q->db, record);
    }

    corm_result_release(res);
    return CORM_OK;
}

corm_err_t corm_create(corm_query_t *q, void *record, int64_t *insert_id) {
    if (q && q->model && q->model->before_create) {
        corm_err_t hook_err = q->model->before_create(q->db, record);
        if (hook_err != CORM_OK) return hook_err;
    }

    corm_strbuf_t sql;
    corm_strbuf_init(&sql);
    q->op = CORM_OP_INSERT;
    corm_err_t err = query_exec(q, &sql);
    if (err) { corm_strbuf_free(&sql); return err; }

    /* Build bind params from record */
    for (int i = 0; i < q->model->field_count; i++) {
        corm_field_t *f = &q->model->fields[i];
        if (f->flags & CORM_FLAG_AUTOINC) continue;
        corm_value_t val = corm_field_get_value(record, f);
        corm_query_bind(q, val);
    }

    err = q->db->backend->exec(q->db, corm_strbuf_cstr(&sql),
                               q->params, q->param_count);
    corm_strbuf_free(&sql);

    if (err == CORM_OK) {
        if (insert_id) *insert_id = q->db->backend->last_insert_id(q->db);
        if (q && q->model && q->model->after_create) {
            q->model->after_create(q->db, record);
        }
    }

    return err;
}

corm_err_t corm_update(corm_query_t *q, int *affected) {
    if (q && q->model && q->model->before_update) {
        corm_err_t hook_err = q->model->before_update(q->db, NULL);
        if (hook_err != CORM_OK) return hook_err;
    }

    corm_strbuf_t sql;
    corm_strbuf_init(&sql);
    q->op = CORM_OP_UPDATE;
    corm_err_t err = query_exec(q, &sql);
    if (err) { corm_strbuf_free(&sql); return err; }

    err = q->db->backend->exec(q->db, corm_strbuf_cstr(&sql),
                               q->params, q->param_count);
    corm_strbuf_free(&sql);

    if (err == CORM_OK) {
        if (affected) *affected = q->db->backend->rows_affected(q->db);
        if (q && q->model && q->model->after_update) {
            q->model->after_update(q->db, NULL);
        }
    }

    return err;
}

corm_err_t corm_delete(corm_query_t *q, int *affected) {
    if (q && q->model && q->model->before_delete) {
        corm_err_t hook_err = q->model->before_delete(q->db, NULL);
        if (hook_err != CORM_OK) return hook_err;
    }

    corm_strbuf_t sql;
    corm_strbuf_init(&sql);
    q->op = CORM_OP_DELETE;
    corm_err_t err = query_exec(q, &sql);
    if (err) { corm_strbuf_free(&sql); return err; }

    err = q->db->backend->exec(q->db, corm_strbuf_cstr(&sql),
                               q->params, q->param_count);
    corm_strbuf_free(&sql);

    if (err == CORM_OK) {
        if (affected) *affected = q->db->backend->rows_affected(q->db);
        if (q && q->model && q->model->after_delete) {
            q->model->after_delete(q->db, NULL);
        }
    }

    return err;
}

/* ── High-level convenience ── */

corm_err_t corm_find_all(corm_t *db, corm_model_t *model,
                         const char *where, void *records, int *count) {
    corm_query_t *q = corm_query_new(db, model);
    if (!q) return CORM_ERR_NOMEM;
    if (where) corm_query_where(q, where);

    corm_result_t *res = NULL;
    corm_err_t err = corm_find(q, &res);
    if (err) { corm_query_free(q); return err; }

    if (res && count) *count = res->row_count;

    /* Copy rows into records array */
    if (res && records) {
        uint8_t *ptr = (uint8_t *)records;
        for (int r = 0; r < res->row_count; r++) {
            for (int i = 0; i < model->field_count; i++) {
                corm_field_t *f = &model->fields[i];
                for (int j = 0; j < res->column_count; j++) {
                    if (strcmp(res->column_names[j], f->name) == 0) {
                        corm_field_set_value(ptr, f, &res->rows[r][j]);
                        break;
                    }
                }
            }
            if (model && model->after_find) {
                model->after_find(db, ptr);
            }
            ptr += model->struct_size;
        }
    }

    corm_result_release(res);
    corm_query_free(q);
    return CORM_OK;
}

corm_err_t corm_create_one(corm_t *db, corm_model_t *model,
                           void *record, int64_t *insert_id) {
    corm_query_t *q = corm_query_new(db, model);
    if (!q) return CORM_ERR_NOMEM;
    corm_err_t err = corm_create(q, record, insert_id);
    corm_query_free(q);
    return err;
}

corm_err_t corm_create_batch(corm_t *db, corm_model_t *model, void *records, int count, int batch_size, int *inserted_count) {
    if (!db || !model || !records || count <= 0) return CORM_ERR_NULL;
    if (batch_size <= 0) batch_size = 100;

    int total_inserted = 0;
    char *bytes = (char*)records;

    for (int i = 0; i < count; i += batch_size) {
        int current_batch = (i + batch_size > count) ? (count - i) : batch_size;
        
        corm_begin(db);
        for (int j = 0; j < current_batch; j++) {
            void *rec = bytes + (i + j) * model->struct_size;
            int64_t id = 0;
            corm_err_t err = corm_create_one(db, model, rec, &id);
            if (err != CORM_OK) {
                corm_rollback(db);
                if (inserted_count) *inserted_count = total_inserted;
                return err;
            }
            total_inserted++;
        }
        corm_commit(db);
    }

    if (inserted_count) *inserted_count = total_inserted;
    return CORM_OK;
}
