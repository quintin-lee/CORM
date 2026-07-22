#include "corm_pub.h"

/* ── SQL builder: builds SQL strings from query state ── */

corm_err_t corm_build_sql(corm_query_t *q, corm_strbuf_t *sql) {
    switch (q->op) {
        case CORM_OP_SELECT: {
            corm_strbuf_append(sql, "SELECT ");
            if (q->select_cols.len > 0)
                corm_strbuf_append(sql, corm_strbuf_cstr(&q->select_cols));
            else
                corm_strbuf_append(sql, "*");
            corm_strbuf_appendf(sql, " FROM %s", q->model->table_name);
            if (q->joins.len > 0)
                corm_strbuf_appendf(sql, " %s", corm_strbuf_cstr(&q->joins));
            if (q->where.len > 0)
                corm_strbuf_appendf(sql, " WHERE %s", corm_strbuf_cstr(&q->where));
            if (q->group.len > 0)
                corm_strbuf_appendf(sql, " GROUP BY %s", corm_strbuf_cstr(&q->group));
            if (q->having.len > 0)
                corm_strbuf_appendf(sql, " HAVING %s", corm_strbuf_cstr(&q->having));
            if (q->order.len > 0)
                corm_strbuf_appendf(sql, " ORDER BY %s", corm_strbuf_cstr(&q->order));
            if (q->limit > 0)
                corm_strbuf_appendf(sql, " LIMIT %d", q->limit);
            if (q->offset > 0)
                corm_strbuf_appendf(sql, " OFFSET %d", q->offset);
            return CORM_OK;
        }
        case CORM_OP_INSERT: {
            corm_strbuf_appendf(sql, "INSERT INTO %s (", q->model->table_name);
            int count = 0;
            for (int i = 0; i < q->model->field_count; i++) {
                corm_field_t *f = &q->model->fields[i];
                if (f->flags & CORM_FLAG_AUTOINC) continue;
                if (count > 0) corm_strbuf_append(sql, ", ");
                corm_strbuf_append(sql, f->name);
                count++;
            }
            corm_strbuf_append(sql, ") VALUES (");
            for (int i = 0, j = 0; i < q->model->field_count; i++) {
                corm_field_t *f = &q->model->fields[i];
                if (f->flags & CORM_FLAG_AUTOINC) continue;
                if (j > 0) corm_strbuf_append(sql, ", ");
                corm_strbuf_append(sql, "?");
                j++;
            }
            corm_strbuf_append(sql, ")");
            return CORM_OK;
        }
        case CORM_OP_UPDATE: {
            corm_strbuf_appendf(sql, "UPDATE %s SET ", q->model->table_name);
            if (q->set_clause.len > 0)
                corm_strbuf_append(sql, corm_strbuf_cstr(&q->set_clause));
            else {
                /* SET all non-PK fields */
                int count = 0;
                for (int i = 0; i < q->model->field_count; i++) {
                    corm_field_t *f = &q->model->fields[i];
                    if (f->flags & CORM_FLAG_PRIMARY) continue;
                    if (count > 0) corm_strbuf_append(sql, ", ");
                    corm_strbuf_appendf(sql, "%s = ?", f->name);
                    count++;
                }
            }
            if (q->where.len > 0)
                corm_strbuf_appendf(sql, " WHERE %s", corm_strbuf_cstr(&q->where));
            return CORM_OK;
        }
        case CORM_OP_DELETE: {
            corm_strbuf_appendf(sql, "DELETE FROM %s", q->model->table_name);
            if (q->where.len > 0)
                corm_strbuf_appendf(sql, " WHERE %s", corm_strbuf_cstr(&q->where));
            return CORM_OK;
        }
    }
    return CORM_ERR_GENERIC;
}
