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

  int pk_count = 0;
  for (int i = 0; i < model->field_count; i++) {
    if (model->fields[i].flags & CORM_FLAG_PRIMARY)
      pk_count++;
  }

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
      if (backend == CORM_BACKEND_MYSQL) {
        /* MySQL needs type before AUTO_INCREMENT, e.g. "INTEGER AUTO_INCREMENT"
         */
        char type_buf[64];
        corm_dialect_type_name_str(backend, f->type, f->size, type_buf,
                                   sizeof(type_buf));
        corm_strbuf_appendf(&sql, " %s AUTO_INCREMENT", type_buf);
      } else {
        corm_strbuf_appendf(&sql, " %s", corm_dialect_autoinc(backend));
        continue; /* autoinc dialect already defines the full column */
      }
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

    /* Primary key constraint — inline for single PK */
    if ((f->flags & CORM_FLAG_PRIMARY) && pk_count == 1) {
      corm_strbuf_append(&sql, " PRIMARY KEY");
    }

    /* Foreign key reference — format: "referenced_table.referenced_column" */
    if (f->fk_ref) {
      corm_strbuf_append(&sql, " REFERENCES ");
      const char *dot = strchr(f->fk_ref, '.');
      if (dot) {
        corm_strbuf_appendn(&sql, f->fk_ref, (size_t)(dot - f->fk_ref));
        corm_strbuf_append(&sql, "(");
        corm_strbuf_append(&sql, dot + 1);
        corm_strbuf_append(&sql, ")");
      } else {
        corm_strbuf_append(&sql, f->fk_ref);
        corm_strbuf_append(&sql, "(id)");
      }
    }
  }

  /* Composite primary key if multiple PK fields defined */
  if (pk_count > 1) {
    corm_strbuf_append(&sql, ", PRIMARY KEY (");
    int added = 0;
    for (int i = 0; i < model->field_count; i++) {
      corm_field_t *f = &model->fields[i];
      if (f->flags & CORM_FLAG_PRIMARY) {
        if (added > 0)
          corm_strbuf_append(&sql, ", ");
        corm_strbuf_append(&sql, corm_dialect_quote(backend, f->name));
        corm_strbuf_append(&sql, f->name);
        corm_strbuf_append(&sql, corm_dialect_quote(backend, f->name));
        added++;
      }
    }
    corm_strbuf_append(&sql, ")");
  }

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
    corm_err_t ct_err = create_table(db, backend, m);
    if (ct_err != CORM_OK)
      return ct_err;

    /* Introspect existing columns for smart diff */
    corm_column_info_t *existing = NULL;
    int existing_count = 0;
    corm_err_t desc_err = CORM_ERR_GENERIC;
    if (db->backend->describe_table)
      desc_err = db->backend->describe_table(db, m->table_name, &existing,
                                             &existing_count);

    /* Incremental column additions — skip columns that already exist */
    for (int j = 0; j < m->field_count; j++) {
      corm_field_t *f = &m->fields[j];
      if (f->flags & CORM_FLAG_PRIMARY)
        continue;

      /* Skip if column already exists in the table */
      if (desc_err == CORM_OK && existing) {
        int found = 0;
        for (int k = 0; k < existing_count; k++) {
          if (strcmp(existing[k].name, f->name) == 0) {
            found = 1;
            break;
          }
        }
        if (found)
          continue;
      }

      char type_buf[64];
      corm_dialect_type_name_str(backend, f->type, f->size, type_buf,
                                 sizeof(type_buf));

      corm_strbuf_t alter_buf;
      corm_strbuf_init(&alter_buf);
      const char *lq = corm_dialect_quote(backend, m->table_name);

      corm_strbuf_appendf(&alter_buf, "ALTER TABLE %s%s%s ADD COLUMN %s%s%s %s",
                          lq, m->table_name, lq, lq, f->name, lq, type_buf);

      if (f->flags & CORM_FLAG_NOT_NULL)
        corm_strbuf_append(&alter_buf, " NOT NULL");
      if (f->flags & CORM_FLAG_UNIQUE)
        corm_strbuf_append(&alter_buf, " UNIQUE");
      if (f->default_value && f->default_value[0])
        corm_strbuf_appendf(&alter_buf, " DEFAULT %s", f->default_value);

      corm_strbuf_append(&alter_buf, ";");
      corm_exec(db, corm_strbuf_cstr(&alter_buf));
      corm_strbuf_free(&alter_buf);
    }

    corm_column_info_free(existing, existing_count);

    /* Create indexes for INDEX-flagged fields */
    for (int j = 0; j < m->field_count; j++) {
      corm_field_t *f = &m->fields[j];
      if (!(f->flags & CORM_FLAG_INDEX))
        continue;

      corm_strbuf_t idx_buf;
      corm_strbuf_init(&idx_buf);
      const char *tq = corm_dialect_quote(backend, m->table_name);
      const char *fq = corm_dialect_quote(backend, f->name);
      corm_strbuf_appendf(
          &idx_buf,
          "CREATE INDEX IF NOT EXISTS %sidx_%s_%s%s ON %s%s%s(%s%s%s)", tq,
          m->table_name, f->name, tq, tq, m->table_name, tq, fq, f->name, fq);
      corm_exec(db, corm_strbuf_cstr(&idx_buf));
      corm_strbuf_free(&idx_buf);
    }
  }

  return CORM_OK;
}
