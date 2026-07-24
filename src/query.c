#include "internal/corm_internal.h"
#include <stdlib.h>
#include <string.h>

/* ── Query builder ── */

static void corm_param_deep_copy(corm_value_t *val) {
  if (val->type == CORM_TEXT || val->type == CORM_STRING) {
    if (val->v.s)
      val->v.s = strdup(val->v.s);
  } else if (val->type == CORM_BLOB) {
    if (val->v.blob.data && val->v.blob.len > 0) {
      void *copy = malloc(val->v.blob.len);
      if (copy) {
        memcpy(copy, val->v.blob.data, val->v.blob.len);
        val->v.blob.data = copy;
      }
    }
  }
}

static void corm_param_free(corm_value_t *val) {
  if ((val->type == CORM_TEXT || val->type == CORM_STRING) && val->v.s) {
    free(val->v.s);
    val->v.s = NULL;
  }
  if (val->type == CORM_BLOB && val->v.blob.data) {
    free(val->v.blob.data);
    val->v.blob.data = NULL;
  }
}

corm_query_t *corm_query_new(corm_t *db, corm_model_t *model) {
  corm_query_t *q = (corm_query_t *)calloc(1, sizeof(corm_query_t));
  if (!q)
    return NULL;
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
  if (!q)
    return;
  corm_strbuf_free(&q->select_cols);
  corm_strbuf_free(&q->where);
  corm_strbuf_free(&q->order);
  corm_strbuf_free(&q->group);
  corm_strbuf_free(&q->having);
  corm_strbuf_free(&q->joins);
  corm_strbuf_free(&q->set_clause);
  for (int i = 0; i < q->param_count; i++)
    corm_param_free(&q->params[i]);
  free(q->params);
  free(q);
}

void corm_query_reset(corm_query_t *q) {
  if (!q)
    return;
  q->op = CORM_OP_SELECT;
  q->limit = 0;
  q->offset = 0;
  for (int i = 0; i < q->param_count; i++)
    corm_param_free(&q->params[i]);
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
  /* Variadic params intentionally unsupported — use corm_query_bind()
   * explicitly */
  return q;
}

corm_query_t *corm_query_or_where(corm_query_t *q, const char *condition, ...) {
  if (q->where.len > 0)
    corm_strbuf_append(&q->where, " OR ");
  corm_strbuf_append(&q->where, condition);
  /* Variadic params intentionally unsupported — use corm_query_bind()
   * explicitly */
  return q;
}

corm_query_t *corm_query_where_null(corm_query_t *q, const char *field) {
  if (!q || !field)
    return q;
  if (q->where.len > 0)
    corm_strbuf_append(&q->where, " AND ");
  corm_strbuf_append(&q->where, field);
  corm_strbuf_append(&q->where, " IS NULL");
  return q;
}

corm_query_t *corm_query_where_not_null(corm_query_t *q, const char *field) {
  if (!q || !field)
    return q;
  if (q->where.len > 0)
    corm_strbuf_append(&q->where, " AND ");
  corm_strbuf_append(&q->where, field);
  corm_strbuf_append(&q->where, " IS NOT NULL");
  return q;
}

corm_query_t *corm_query_where_in(corm_query_t *q, const char *field,
                                  corm_value_t *vals, int count) {
  if (!q || !field || !vals || count <= 0)
    return q;
  if (q->where.len > 0)
    corm_strbuf_append(&q->where, " AND ");
  corm_strbuf_append(&q->where, field);
  corm_strbuf_append(&q->where, " IN (");

  corm_backend_type_t bt = q->db ? q->db->backend->type : CORM_BACKEND_SQLITE;
  for (int i = 0; i < count; i++) {
    if (i > 0)
      corm_strbuf_append(&q->where, ", ");
    char ph_buf[16];
    corm_dialect_placeholder_str(bt, q->param_count, ph_buf, sizeof(ph_buf));
    corm_strbuf_append(&q->where, ph_buf);
    corm_query_bind(q, vals[i]);
  }
  corm_strbuf_append(&q->where, ")");
  return q;
}

corm_query_t *corm_query_where_between(corm_query_t *q, const char *field,
                                       corm_value_t min_val,
                                       corm_value_t max_val) {
  if (!q || !field)
    return q;
  if (q->where.len > 0)
    corm_strbuf_append(&q->where, " AND ");
  corm_strbuf_append(&q->where, field);
  corm_strbuf_append(&q->where, " BETWEEN ");

  corm_backend_type_t bt = q->db ? q->db->backend->type : CORM_BACKEND_SQLITE;

  char ph1[16], ph2[16];
  corm_dialect_placeholder_str(bt, q->param_count, ph1, sizeof(ph1));
  corm_strbuf_append(&q->where, ph1);
  corm_query_bind(q, min_val);

  corm_strbuf_append(&q->where, " AND ");

  corm_dialect_placeholder_str(bt, q->param_count, ph2, sizeof(ph2));
  corm_strbuf_append(&q->where, ph2);
  corm_query_bind(q, max_val);

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
    corm_value_t *tmp = (corm_value_t *)realloc(
        q->params, (size_t)new_cap * sizeof(corm_value_t));
    if (!tmp)
      return q;
    q->params = tmp;
    q->param_cap = new_cap;
  }
  corm_param_deep_copy(&val);
  q->params[q->param_count++] = val;
  return q;
}

corm_query_t *corm_query_set(corm_query_t *q, const char *field,
                             corm_value_t val) {
  if (q->set_clause.len > 0)
    corm_strbuf_append(&q->set_clause, ", ");
  const char *qchar = corm_dialect_quote(q->db->backend->type, field);
  corm_strbuf_append(&q->set_clause, qchar);
  corm_strbuf_append(&q->set_clause, field);
  corm_strbuf_append(&q->set_clause, qchar);
  corm_strbuf_append(&q->set_clause, " = ");
  char ph_buf[16];
  corm_dialect_placeholder_str(q->db->backend->type, q->param_count, ph_buf,
                               sizeof(ph_buf));
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

corm_query_t *corm_query_unscoped(corm_query_t *q) {
  if (q)
    q->unscoped = true;
  return q;
}

corm_query_t *corm_query_preload(corm_query_t *q, const char *relation_name) {
  (void)relation_name;
  return q;
}

/* ── Execution ── */

static corm_err_t query_exec(corm_query_t *q, corm_strbuf_t *sql) {
  corm_backend_type_t bt = q->db ? q->db->backend->type : CORM_BACKEND_SQLITE;
  corm_err_t err = corm_build_sql(q, sql, bt);
  if (err)
    return err;
  return CORM_OK;
}

corm_err_t corm_find(corm_query_t *q, corm_result_t **out) {
  if (q && q->model && !q->unscoped) {
    for (int i = 0; i < q->model->field_count; i++) {
      if (q->model->fields[i].flags & CORM_FLAG_SOFT_DELETE) {
        corm_query_where_null(q, q->model->fields[i].name);
        break;
      }
    }
  }

  corm_strbuf_t sql;
  corm_strbuf_init(&sql);
  q->op = CORM_OP_SELECT;
  corm_err_t err = query_exec(q, &sql);
  if (err) {
    corm_strbuf_free(&sql);
    return err;
  }

  err = q->db->backend->query(q->db, corm_strbuf_cstr(&sql), q->params,
                              q->param_count, out);
  corm_strbuf_free(&sql);
  return err;
}

corm_err_t corm_first(corm_query_t *q, void *record) {
  q->limit = 1;
  q->offset = 0;
  corm_result_t *res = NULL;
  corm_err_t err = corm_find(q, &res);
  if (err)
    return err;
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
    if (hook_err != CORM_OK)
      return hook_err;
  }

  corm_strbuf_t sql;
  corm_strbuf_init(&sql);
  q->op = CORM_OP_INSERT;
  corm_err_t err = query_exec(q, &sql);
  if (err) {
    corm_strbuf_free(&sql);
    return err;
  }

  /* Build bind params from record */
  for (int i = 0; i < q->model->field_count; i++) {
    corm_field_t *f = &q->model->fields[i];
    if (f->flags & CORM_FLAG_AUTOINC)
      continue;
    corm_value_t val = corm_field_get_value(record, f);
    corm_query_bind(q, val);
  }

  err = q->db->backend->exec(q->db, corm_strbuf_cstr(&sql), q->params,
                             q->param_count);
  corm_strbuf_free(&sql);

  if (err == CORM_OK) {
    if (insert_id)
      *insert_id = q->db->backend->last_insert_id(q->db);
    if (q && q->model && q->model->after_create) {
      q->model->after_create(q->db, record);
    }
  }

  return err;
}

/* Internal: update with the actual record pointer passed to hooks */
static corm_err_t corm_update_internal(corm_query_t *q, int *affected,
                                       void *record) {
  if (q && q->model && q->model->before_update) {
    corm_err_t hook_err = q->model->before_update(q->db, record);
    if (hook_err != CORM_OK)
      return hook_err;
  }

  corm_strbuf_t sql;
  corm_strbuf_init(&sql);
  q->op = CORM_OP_UPDATE;
  corm_err_t err = query_exec(q, &sql);
  if (err) {
    corm_strbuf_free(&sql);
    return err;
  }

  err = q->db->backend->exec(q->db, corm_strbuf_cstr(&sql), q->params,
                             q->param_count);
  corm_strbuf_free(&sql);

  if (err == CORM_OK) {
    if (affected)
      *affected = q->db->backend->rows_affected(q->db);
    if (q && q->model && q->model->after_update) {
      q->model->after_update(q->db, record);
    }
  }

  return err;
}

corm_err_t corm_update(corm_query_t *q, int *affected) {
  return corm_update_internal(q, affected, NULL);
}

/* Internal: delete with the actual record pointer passed to hooks */
static corm_err_t corm_delete_internal(corm_query_t *q, int *affected,
                                       void *record) {
  if (q && q->model) {
    for (int i = 0; i < q->model->field_count; i++) {
      if (q->model->fields[i].flags & CORM_FLAG_SOFT_DELETE) {
        corm_value_t ts = {
            .type = CORM_STRING, .is_null = false, .v.s = "deleted"};
        corm_query_set(q, q->model->fields[i].name, ts);
        return corm_update_internal(q, affected, record);
      }
    }
  }

  if (q && q->model && q->model->before_delete) {
    corm_err_t hook_err = q->model->before_delete(q->db, record);
    if (hook_err != CORM_OK)
      return hook_err;
  }

  corm_strbuf_t sql;
  corm_strbuf_init(&sql);
  q->op = CORM_OP_DELETE;
  corm_err_t err = query_exec(q, &sql);
  if (err) {
    corm_strbuf_free(&sql);
    return err;
  }

  err = q->db->backend->exec(q->db, corm_strbuf_cstr(&sql), q->params,
                             q->param_count);
  corm_strbuf_free(&sql);

  if (err == CORM_OK) {
    if (affected)
      *affected = q->db->backend->rows_affected(q->db);
    if (q && q->model && q->model->after_delete) {
      q->model->after_delete(q->db, record);
    }
  }

  return err;
}

corm_err_t corm_delete(corm_query_t *q, int *affected) {
  return corm_delete_internal(q, affected, NULL);
}

/* ── High-level convenience ── */

corm_err_t corm_find_all(corm_t *db, corm_model_t *model, const char *where,
                         void *records, int *count) {
  corm_query_t *q = corm_query_new(db, model);
  if (!q)
    return CORM_ERR_NOMEM;
  if (where)
    corm_query_where(q, where);

  corm_result_t *res = NULL;
  corm_err_t err = corm_find(q, &res);
  if (err) {
    corm_query_free(q);
    return err;
  }

  int max_capacity = (count && *count > 0) ? *count : -1;
  int total_rows = res ? res->row_count : 0;
  int copy_count = total_rows;
  if (max_capacity > 0 && copy_count > max_capacity) {
    copy_count = max_capacity;
  }

  if (res && count)
    *count = copy_count;

  /* Copy rows into records array */
  if (res && records) {
    uint8_t *ptr = (uint8_t *)records;
    for (int r = 0; r < copy_count; r++) {
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

corm_err_t corm_create_one(corm_t *db, corm_model_t *model, void *record,
                           int64_t *insert_id) {
  corm_query_t *q = corm_query_new(db, model);
  if (!q)
    return CORM_ERR_NOMEM;
  corm_err_t err = corm_create(q, record, insert_id);
  corm_query_free(q);
  return err;
}

corm_err_t corm_find_one(corm_t *db, corm_model_t *model, const char *where,
                         void *record) {
  corm_query_t *q = corm_query_new(db, model);
  if (!q)
    return CORM_ERR_NOMEM;
  if (where)
    corm_query_where(q, where);
  corm_err_t err = corm_first(q, record);
  corm_query_free(q);
  return err;
}

corm_err_t corm_count(corm_t *db, corm_model_t *model, const char *where,
                      int *count) {
  if (count)
    *count = 0;
  corm_query_t *q = corm_query_new(db, model);
  if (!q)
    return CORM_ERR_NOMEM;
  corm_query_select(q, "COUNT(*)");
  if (where)
    corm_query_where(q, where);
  corm_result_t *res = NULL;
  corm_err_t err = corm_find(q, &res);
  if (err) {
    corm_result_release(res);
    corm_query_free(q);
    return err;
  }
  if (res && res->row_count > 0 && count)
    *count = (int)res->rows[0][0].v.i;
  corm_result_release(res);
  corm_query_free(q);
  return CORM_OK;
}

corm_err_t corm_create_batch(corm_t *db, corm_model_t *model, void *records,
                             int count, int batch_size, int *inserted_count) {
  if (!db || !model || !records || count <= 0)
    return CORM_ERR_NULL;
  if (batch_size <= 0)
    batch_size = 100;

  /* Count non-AUTOINC fields */
  int val_count = 0;
  for (int i = 0; i < model->field_count; i++) {
    if (!(model->fields[i].flags & CORM_FLAG_AUTOINC))
      val_count++;
  }
  if (val_count <= 0)
    return CORM_ERR_MISMATCH;

  uint8_t *bytes = (uint8_t *)records;
  int total_inserted = 0;

  for (int i = 0; i < count; i += batch_size) {
    int current_batch = (i + batch_size > count) ? (count - i) : batch_size;
    corm_backend_type_t bt = db->backend->type;

    /* Build SQL: INSERT INTO t (c1,c2,...) VALUES (p,p,...),(p,p,...) */
    corm_strbuf_t sql;
    corm_strbuf_init(&sql);
    {
      const char *q = corm_dialect_quote(bt, model->table_name);
      corm_strbuf_appendf(&sql, "INSERT INTO %s%s%s (", q, model->table_name,
                          q);
    }
    int ci = 0;
    for (int j = 0; j < model->field_count; j++) {
      corm_field_t *f = &model->fields[j];
      if (f->flags & CORM_FLAG_AUTOINC)
        continue;
      if (ci > 0)
        corm_strbuf_append(&sql, ", ");
      const char *q = corm_dialect_quote(bt, f->name);
      corm_strbuf_append(&sql, q);
      corm_strbuf_append(&sql, f->name);
      corm_strbuf_append(&sql, q);
      ci++;
    }
    corm_strbuf_append(&sql, ") VALUES ");

    /* Build query for bind params (deep-copy behavior via corm_query_bind) */
    corm_query_t *q = corm_query_new(db, model);
    if (!q) {
      corm_strbuf_free(&sql);
      if (inserted_count)
        *inserted_count = total_inserted;
      return CORM_ERR_NOMEM;
    }

    int param_idx = 0;
    for (int r = 0; r < current_batch; r++) {
      if (r > 0)
        corm_strbuf_append(&sql, ", (");
      else
        corm_strbuf_append(&sql, "(");
      void *rec = bytes + (i + r) * model->struct_size;
      int fi = 0;
      for (int j = 0; j < model->field_count; j++) {
        corm_field_t *f = &model->fields[j];
        if (f->flags & CORM_FLAG_AUTOINC)
          continue;
        if (fi > 0)
          corm_strbuf_append(&sql, ", ");
        char ph_buf[16];
        corm_dialect_placeholder_str(bt, param_idx++, ph_buf, sizeof(ph_buf));
        corm_strbuf_append(&sql, ph_buf);
        fi++;
      }
      corm_strbuf_append(&sql, ")");

      /* Bind params for this row */
      for (int j = 0; j < model->field_count; j++) {
        corm_field_t *f = &model->fields[j];
        if (f->flags & CORM_FLAG_AUTOINC)
          continue;
        corm_value_t val = corm_field_get_value(rec, f);
        corm_query_bind(q, val);
      }
    }

    corm_err_t err = db->backend->exec(db, corm_strbuf_cstr(&sql), q->params,
                                       q->param_count);
    corm_strbuf_free(&sql);
    corm_query_free(q);

    if (err != CORM_OK) {
      if (inserted_count)
        *inserted_count = total_inserted;
      return err;
    }
    total_inserted += current_batch;
  }

  if (inserted_count)
    *inserted_count = total_inserted;
  return CORM_OK;
}

corm_err_t corm_update_batch(corm_t *db, corm_model_t *model, void *records,
                             int count, int *affected_count) {
  if (!db || !model || !records || count <= 0)
    return CORM_ERR_NULL;

  /* Require a primary key — without it every update targets ALL rows */
  int pk_field = -1;
  for (int j = 0; j < model->field_count; j++) {
    if (model->fields[j].flags & CORM_FLAG_PRIMARY) {
      pk_field = j;
      break;
    }
  }
  if (pk_field < 0)
    return CORM_ERR_MISMATCH;

  uint8_t *bytes = (uint8_t *)records;
  int total_affected = 0;
  corm_err_t global_err = CORM_OK;

  corm_begin(db);
  for (int i = 0; i < count; i++) {
    void *rec = bytes + i * model->struct_size;
    corm_query_t *q = corm_query_new(db, model);
    if (!q) {
      corm_rollback(db);
      if (affected_count)
        *affected_count = total_affected;
      return CORM_ERR_NOMEM;
    }

    for (int j = 0; j < model->field_count; j++) {
      corm_field_t *f = &model->fields[j];
      if (f->flags & CORM_FLAG_PRIMARY)
        continue;
      corm_value_t val = corm_field_get_value(rec, f);
      corm_query_set(q, f->name, val);
    }

    corm_field_t *pk = &model->fields[pk_field];
    corm_strbuf_t where;
    corm_strbuf_init(&where);
    corm_strbuf_appendf(&where, "%s = ?", pk->name);
    corm_query_where(q, corm_strbuf_cstr(&where));
    corm_strbuf_free(&where);
    corm_value_t pk_val = corm_field_get_value(rec, pk);
    corm_query_bind(q, pk_val);

    int aff = 0;
    corm_err_t err = corm_update_internal(q, &aff, rec);
    total_affected += aff;
    corm_query_free(q);

    if (err != CORM_OK) {
      global_err = err;
      break;
    }
  }

  if (global_err != CORM_OK)
    corm_rollback(db);
  else
    corm_commit(db);

  if (affected_count)
    *affected_count = total_affected;
  return global_err;
}

corm_err_t corm_delete_batch(corm_t *db, corm_model_t *model, void *records,
                             int count, int *affected_count) {
  if (!db || !model || !records || count <= 0)
    return CORM_ERR_NULL;

  /* Require a primary key — without it every delete targets ALL rows */
  int pk_field = -1;
  for (int j = 0; j < model->field_count; j++) {
    if (model->fields[j].flags & CORM_FLAG_PRIMARY) {
      pk_field = j;
      break;
    }
  }
  if (pk_field < 0)
    return CORM_ERR_MISMATCH;

  int has_soft_delete = 0;
  int soft_delete_idx = -1;
  for (int j = 0; j < model->field_count; j++) {
    if (model->fields[j].flags & CORM_FLAG_SOFT_DELETE) {
      has_soft_delete = 1;
      soft_delete_idx = j;
      break;
    }
  }

  uint8_t *bytes = (uint8_t *)records;
  int total_affected = 0;
  corm_err_t global_err = CORM_OK;

  corm_begin(db);
  for (int i = 0; i < count; i++) {
    void *rec = bytes + i * model->struct_size;
    corm_query_t *q = corm_query_new(db, model);
    if (!q) {
      corm_rollback(db);
      if (affected_count)
        *affected_count = total_affected;
      return CORM_ERR_NOMEM;
    }

    if (has_soft_delete) {
      corm_value_t ts = {
          .type = CORM_STRING, .is_null = false, .v.s = "deleted"};
      corm_query_set(q, model->fields[soft_delete_idx].name, ts);
    }

    corm_field_t *pk = &model->fields[pk_field];
    corm_strbuf_t where;
    corm_strbuf_init(&where);
    corm_strbuf_appendf(&where, "%s = ?", pk->name);
    corm_query_where(q, corm_strbuf_cstr(&where));
    corm_strbuf_free(&where);
    corm_value_t pk_val = corm_field_get_value(rec, pk);
    corm_query_bind(q, pk_val);

    int aff = 0;
    corm_err_t err;
    if (has_soft_delete)
      err = corm_update_internal(q, &aff, rec);
    else
      err = corm_delete_internal(q, &aff, rec);
    total_affected += aff;
    corm_query_free(q);

    if (err != CORM_OK) {
      global_err = err;
      break;
    }
  }

  if (global_err != CORM_OK)
    corm_rollback(db);
  else
    corm_commit(db);

  if (affected_count)
    *affected_count = total_affected;
  return global_err;
}
