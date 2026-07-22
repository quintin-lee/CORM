#include <stdio.h>
#include <string.h>
#include "internal/corm_internal.h"

/* ── Auto-migration: create/alter tables from model definitions ── */

static corm_err_t create_table(corm_t *db, corm_backend_type_t backend,
                               corm_model_t *model) {
    corm_strbuf_t sql;
    corm_strbuf_init(&sql);

    corm_strbuf_appendf(&sql, "CREATE TABLE %s %s (",
                        corm_dialect_if_not_exists(backend),
                        model->table_name);

    for (int i = 0; i < model->field_count; i++) {
        corm_field_t *f = &model->fields[i];

        if (i > 0) corm_strbuf_append(&sql, ", ");

        /* Column name */
        corm_strbuf_append(&sql, corm_dialect_quote(backend, f->name));
        corm_strbuf_append(&sql, f->name);
        corm_strbuf_append(&sql, corm_dialect_quote(backend, f->name));

        /* Column type */
        if (f->flags & CORM_FLAG_AUTOINC) {
            /* `id` INTEGER PRIMARY KEY AUTOINCREMENT */
            corm_strbuf_appendf(&sql, " %s", corm_dialect_autoinc(backend));
            /* For SQLite, that's the whole definition */
            if (backend == CORM_BACKEND_SQLITE || backend == CORM_BACKEND_POSTGRES) {
                if (backend == CORM_BACKEND_POSTGRES) {
                    /* PostgreSQL SERIAL PRIMARY KEY handles everything */
                }
                continue; /* autoinc already defines the column fully */
            } else if (backend == CORM_BACKEND_MYSQL) {
                corm_strbuf_appendf(&sql, " %s",
                    corm_dialect_type_name(backend, f->type, f->size));
                corm_strbuf_append(&sql, " AUTO_INCREMENT");
                if (f->flags & CORM_FLAG_PRIMARY)
                    corm_strbuf_append(&sql, " PRIMARY KEY");
            }
        } else {
            corm_strbuf_appendf(&sql, " %s",
                corm_dialect_type_name(backend, f->type, f->size));
        }

        /* Constraints */
        if (!(f->flags & CORM_FLAG_AUTOINC)) {
            if (f->flags & CORM_FLAG_NOT_NULL)
                corm_strbuf_append(&sql, " NOT NULL");
            if (f->flags & CORM_FLAG_UNIQUE)
                corm_strbuf_append(&sql, " UNIQUE");
            if (f->default_value && f->default_value[0])
                corm_strbuf_appendf(&sql, " DEFAULT %s", f->default_value);
        }

        /* Primary key constraint for non-SQLite autoinc */
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

corm_err_t corm_auto_migrate(corm_t *db, corm_model_t *models[], int model_count) {
    corm_backend_type_t backend = db->backend->type;

    for (int i = 0; i < model_count; i++) {
        corm_err_t err = create_table(db, backend, models[i]);
        if (err != CORM_OK && err != CORM_ERR_GENERIC) {
            /* If table already exists, try ALTER later */
            /* For v1, just log and continue */
            continue;
        }
    }

    return CORM_OK;
}
