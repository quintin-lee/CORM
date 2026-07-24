#include "internal/corm_internal.h"
#include <stdio.h>
#include <string.h>

/* ── Auto-migration: create/alter tables from model definitions ── */

static corm_err_t create_table(corm_t *db, corm_backend_type_t backend,
                               corm_model_t *model) {
  corm_strbuf_t sql;
  corm_strbuf_init(&sql);

  corm_strbuf_appendf(&sql, "CREATE TABLE %s ",
                      corm_dialect_if_not_exists(backend));
  corm_strbuf_append(&sql, corm_dialect_quote(backend, model->table_name));
  corm_strbuf_append(&sql, model->table_name);
  corm_strbuf_append(&sql, corm_dialect_quote(backend, model->table_name));
  corm_strbuf_append(&sql, " (");

  for (int i = 0; i < model->field_count; i++) {
    corm_field_t *f = &model->fields[i];

    if (i > 0)
      corm_strbuf_append(&sql, ", ");

    /* Column name */
    corm_strbuf_append(&sql, corm_dialect_quote(backend, f->name));
    corm_strbuf_append(&sql, f->name);
    corm_strbuf_append(&sql, corm_dialect_quote(backend, f->name));

    /* Column type */
    if (f->flags & CORM_FLAG_AUTOINC) {
      corm_strbuf_appendf(&sql, " %s", corm_dialect_autoinc(backend));
      continue; /* autoinc dialect already defines the full column */
    } else {
      char type_buf[64];
      corm_dialect_type_name_str(backend, f->type, f->size, type_buf,
                                 sizeof(type_buf));
      corm_strbuf_appendf(&sql, " %s", type_buf);
    }

    /* Constraints */
    if (f->flags & CORM_FLAG_NOT_NULL)
      corm_strbuf_append(&sql, " NOT NULL");
    if (f->flags & CORM_FLAG_UNIQUE)
      corm_strbuf_append(&sql, " UNIQUE");
    if (f->default_value && f->default_value[0])
      corm_strbuf_appendf(&sql, " DEFAULT %s", f->default_value);

    /* Primary key constraint for non-autoinc fields */
    if ((f->flags & CORM_FLAG_PRIMARY) && !(f->flags & CORM_FLAG_AUTOINC)) {
      corm_strbuf_append(&sql, " PRIMARY KEY");
    }
  }

  /* Composite primary key if no single PK defined */
  corm_strbuf_append(&sql, ")");

  corm_err_t err = corm_exec(db, corm_strbuf_cstr(&sql));
  corm_strbuf_free(&sql);
  return err;
}

corm_err_t corm_auto_migrate(corm_t *db, corm_model_t *models[],
                             int model_count) {
  if (!db || !models)
    return CORM_ERR_NULL;
  corm_backend_type_t backend = db->backend->type;

  for (int i = 0; i < model_count; i++) {
    corm_model_t *m = models[i];
    create_table(db, backend, m);

    /* Incremental column additions for existing tables */
    for (int j = 0; j < m->field_count; j++) {
      corm_field_t *f = &m->fields[j];
      if (f->flags & CORM_FLAG_PRIMARY)
        continue; // Skip primary key in ALTER TABLE

      char alter_sql[512];
      char type_buf[64];
      corm_dialect_type_name_str(backend, f->type, f->size, type_buf,
                                 sizeof(type_buf));

      const char *lq = corm_dialect_quote(backend, m->table_name);
      snprintf(alter_sql, sizeof(alter_sql),
               "ALTER TABLE %s%s%s ADD COLUMN %s%s%s %s;", lq, m->table_name,
               lq, lq, f->name, lq, type_buf);
      /* corm_exec will ignore error if column already exists */
      corm_exec(db, alter_sql);
    }
  }

  return CORM_OK;
}
