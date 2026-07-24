#include "corm_pub.h"

/* ── SQL builder: builds SQL strings from query state ── */

/* Append a dialect-quoted identifier */
static void qident(corm_strbuf_t *sql, corm_backend_type_t bt,
                   const char *name) {
  const char *q = corm_dialect_quote(bt, name);
  corm_strbuf_append(sql, q);
  corm_strbuf_append(sql, name);
  corm_strbuf_append(sql, q);
}

/* Append `text` rewriting `?` to dialect-aware placeholders starting at
 * `start_index`. Returns the number of placeholders consumed. */
static int append_with_placeholders(corm_strbuf_t *sql, corm_backend_type_t bt,
                                    const char *text, int start_index) {
  int pi = start_index;
  char ph_buf[16];
  while (*text) {
    const char *qm = strchr(text, '?');
    if (qm) {
      corm_strbuf_appendn(sql, text, (size_t)(qm - text));
      corm_dialect_placeholder_str(bt, pi++, ph_buf, sizeof(ph_buf));
      corm_strbuf_append(sql, ph_buf);
      text = qm + 1;
    } else {
      corm_strbuf_append(sql, text);
      break;
    }
  }
  return pi - start_index;
}

corm_err_t corm_build_sql(corm_query_t *q, corm_strbuf_t *sql,
                          corm_backend_type_t bt) {
  switch (q->op) {
  case CORM_OP_SELECT: {
    corm_strbuf_append(sql, "SELECT ");
    if (q->distinct)
      corm_strbuf_append(sql, "DISTINCT ");
    if (q->select_cols.len > 0)
      corm_strbuf_append(sql, corm_strbuf_cstr(&q->select_cols));
    else
      corm_strbuf_append(sql, "*");
    corm_strbuf_append(sql, " FROM ");
    qident(sql, bt, q->model->table_name);
    if (q->joins.len > 0)
      corm_strbuf_appendf(sql, " %s", corm_strbuf_cstr(&q->joins));
    if (q->where.len > 0) {
      corm_strbuf_append(sql, " WHERE ");
      append_with_placeholders(sql, bt, corm_strbuf_cstr(&q->where), 0);
    }
    if (q->group.len > 0)
      corm_strbuf_appendf(sql, " GROUP BY %s", corm_strbuf_cstr(&q->group));
    if (q->having.len > 0)
      corm_strbuf_appendf(sql, " HAVING %s", corm_strbuf_cstr(&q->having));
    if (q->order.len > 0)
      corm_strbuf_appendf(sql, " ORDER BY %s", corm_strbuf_cstr(&q->order));
    if (q->limit > 0 || q->offset > 0) {
      /* All backends: inline literal numbers — LIMIT/OFFSET are always
       * integers, no injection risk, avoids PG $N placeholder complexity */
      corm_strbuf_appendf(sql, " LIMIT %d", q->limit);
      if (q->offset > 0)
        corm_strbuf_appendf(sql, " OFFSET %d", q->offset);
    }
    return CORM_OK;
  }
  case CORM_OP_INSERT: {
    corm_strbuf_append(sql, "INSERT INTO ");
    qident(sql, bt, q->model->table_name);
    corm_strbuf_append(sql, " (");
    int count = 0;
    for (int i = 0; i < q->model->field_count; i++) {
      corm_field_t *f = &q->model->fields[i];
      if (f->flags & CORM_FLAG_AUTOINC)
        continue;
      if (count > 0)
        corm_strbuf_append(sql, ", ");
      qident(sql, bt, f->name);
      count++;
    }
    corm_strbuf_append(sql, ") VALUES (");
    char ph_buf[16];
    for (int i = 0, j = 0; i < q->model->field_count; i++) {
      corm_field_t *f = &q->model->fields[i];
      if (f->flags & CORM_FLAG_AUTOINC)
        continue;
      if (j > 0)
        corm_strbuf_append(sql, ", ");
      corm_dialect_placeholder_str(bt, j, ph_buf, sizeof(ph_buf));
      corm_strbuf_append(sql, ph_buf);
      j++;
    }
    corm_strbuf_append(sql, ")");

    /* PostgreSQL: append RETURNING pk_col so last_insert_id works */
    if (bt == CORM_BACKEND_POSTGRES && q->model->primary_key) {
      corm_strbuf_append(sql, " RETURNING ");
      qident(sql, bt, q->model->primary_key->name);
    }
    return CORM_OK;
  }
  case CORM_OP_UPDATE: {
    corm_strbuf_append(sql, "UPDATE ");
    qident(sql, bt, q->model->table_name);
    corm_strbuf_append(sql, " SET ");
    if (q->set_clause.len > 0) {
      append_with_placeholders(sql, bt, corm_strbuf_cstr(&q->set_clause), 0);
    } else {
      /* SET all non-PK fields */
      int count = 0;
      char ph_buf[16];
      for (int i = 0; i < q->model->field_count; i++) {
        corm_field_t *f = &q->model->fields[i];
        if (f->flags & CORM_FLAG_PRIMARY)
          continue;
        if (count > 0)
          corm_strbuf_append(sql, ", ");
        qident(sql, bt, f->name);
        corm_strbuf_append(sql, " = ");
        corm_dialect_placeholder_str(bt, count, ph_buf, sizeof(ph_buf));
        corm_strbuf_append(sql, ph_buf);
        count++;
      }
    }
    if (q->where.len > 0) {
      int where_offset;
      if (q->set_clause.len > 0) {
        const char *s = corm_strbuf_cstr(&q->set_clause);
        int n = 0;
        while (*s) {
          if (*(s++) == '?')
            n++;
        }
        where_offset = n;
      } else {
        int n = 0;
        for (int i = 0; i < q->model->field_count; i++) {
          if (!(q->model->fields[i].flags & CORM_FLAG_PRIMARY))
            n++;
        }
        where_offset = n;
      }
      corm_strbuf_append(sql, " WHERE ");
      append_with_placeholders(sql, bt, corm_strbuf_cstr(&q->where),
                               where_offset);
    }
    return CORM_OK;
  }
  case CORM_OP_DELETE: {
    corm_strbuf_append(sql, "DELETE FROM ");
    qident(sql, bt, q->model->table_name);
    if (q->where.len > 0) {
      corm_strbuf_append(sql, " WHERE ");
      append_with_placeholders(sql, bt, corm_strbuf_cstr(&q->where), 0);
    }
    return CORM_OK;
  }
  }
  return CORM_ERR_GENERIC;
}
