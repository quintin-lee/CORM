#include <stdlib.h>
#include <string.h>
#include "corm_pub.h"

#include "internal/corm_internal.h"

corm_err_t corm_model_registry_init(corm_registry_t *reg) {
    corm_err_t err;
    err = corm_hash_init(&reg->models_by_table, 32);
    if (err) return err;
    err = corm_hash_init(&reg->models_by_name, 32);
    if (err) { corm_hash_free(&reg->models_by_table); return err; }
    return CORM_OK;
}

void corm_model_registry_free(corm_registry_t *reg) {
    corm_hash_free(&reg->models_by_table);
    corm_hash_free(&reg->models_by_name);
}

corm_err_t corm_register_model(corm_t *db, corm_model_t *model) {
    return corm_hash_insert(&db->registry.models_by_table,
                            model->table_name, model);
}

corm_model_t *corm_find_model(corm_t *db, const char *table_name) {
    return (corm_model_t *)corm_hash_find(
        &db->registry.models_by_table, table_name);
}

corm_field_t *corm_find_field(corm_model_t *model, const char *field_name) {
    for (int i = 0; i < model->field_count; i++) {
        if (strcmp(model->fields[i].name, field_name) == 0)
            return &model->fields[i];
    }
    return NULL;
}

/* Extract a field value from a struct by field descriptor */
corm_value_t corm_field_get_value(void *record, corm_field_t *field) {
    corm_value_t val;
    val.is_null = false;
    val.type = field->type;
    uint8_t *ptr = (uint8_t *)record + field->offset;

    switch (field->type) {
        case CORM_INT:
            val.v.i = *(int *)ptr;
            break;
        case CORM_INT64:
            val.v.i = *(int64_t *)ptr;
            break;
        case CORM_FLOAT:
            val.v.f = *(float *)ptr;
            break;
        case CORM_DOUBLE:
            val.v.f = *(double *)ptr;
            break;
        case CORM_STRING:
            val.v.s = (char *)ptr; /* points into struct */
            break;
        case CORM_TEXT:
            val.v.s = *(char **)ptr;
            break;
        case CORM_BOOL:
            val.v.b = *(bool *)ptr;
            break;
        case CORM_BLOB:
            val.v.blob.data = ptr;
            val.v.blob.len = field->size;
            break;
    }
    return val;
}

void corm_field_set_value(void *record, corm_field_t *field, corm_value_t *val) {
    uint8_t *ptr = (uint8_t *)record + field->offset;
    switch (field->type) {
        case CORM_INT:
            *(int *)ptr = (int)val->v.i;
            break;
        case CORM_INT64:
            *(int64_t *)ptr = val->v.i;
            break;
        case CORM_FLOAT:
            *(float *)ptr = (float)val->v.f;
            break;
        case CORM_DOUBLE:
            *(double *)ptr = val->v.f;
            break;
        case CORM_STRING:
            if (val->v.s && field->size > 0) {
                strncpy((char *)ptr, val->v.s, field->size - 1);
                ((char *)ptr)[field->size - 1] = '\0';
            }
            break;
        case CORM_TEXT: {
            char *old = *(char **)ptr;
            *(char **)ptr = val->v.s ? strdup(val->v.s) : NULL;
            free(old);
            break;
        }
        case CORM_BOOL:
            *(bool *)ptr = val->v.b;
            break;
        case CORM_BLOB:
            if (val->v.blob.data && val->v.blob.len <= field->size) {
                memcpy(ptr, val->v.blob.data, val->v.blob.len);
            }
            break;
    }
}
