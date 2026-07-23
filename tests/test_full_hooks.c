#include "corm_pub.h"
#include <assert.h>
#include <stdio.h>

static bool before_update_called = false;
static bool after_update_called = false;
static bool before_delete_called = false;
static bool after_delete_called = false;
static bool after_find_called = false;

typedef struct {
    int id;
    char name[64];
} HookRecord;

static corm_field_t rec_fields[] = {
    CORM_FIELD(HookRecord, id, CORM_INT, CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC, NULL),
    CORM_FIELD(HookRecord, name, CORM_STRING, 0, NULL),
};

static corm_err_t on_before_update(corm_t *db, void *record) { (void)db; (void)record; before_update_called = true; return CORM_OK; }
static corm_err_t on_after_update(corm_t *db, void *record) { (void)db; (void)record; after_update_called = true; return CORM_OK; }
static corm_err_t on_before_delete(corm_t *db, void *record) { (void)db; (void)record; before_delete_called = true; return CORM_OK; }
static corm_err_t on_after_delete(corm_t *db, void *record) { (void)db; (void)record; after_delete_called = true; return CORM_OK; }
static corm_err_t on_after_find(corm_t *db, void *record) { (void)db; (void)record; after_find_called = true; return CORM_OK; }

static corm_model_t full_hook_model = {
    .table_name = "full_hook_records",
    .struct_size = sizeof(HookRecord),
    .fields = rec_fields,
    .field_count = 2,
    .primary_key = &rec_fields[0],
    .before_update = on_before_update,
    .after_update = on_after_update,
    .before_delete = on_before_delete,
    .after_delete = on_after_delete,
    .after_find = on_after_find,
};

void test_all_lifecycle_hooks(void) {
    corm_t *db;
    corm_open("sqlite3://:memory:", &db);
    corm_register_model(db, &full_hook_model);
    corm_model_t *models[] = { &full_hook_model };
    corm_auto_migrate(db, models, 1);

    HookRecord rec = { .name = "Initial" };
    int64_t id = 0;
    corm_create_one(db, &full_hook_model, &rec, &id);

    // Test first & after_find hook
    corm_query_t *q1 = corm_query_new(db, &full_hook_model);
    HookRecord found_rec;
    corm_err_t err = corm_first(q1, &found_rec);
    assert(err == CORM_OK);
    assert(after_find_called == true);
    corm_query_free(q1);

    // Test update hooks
    corm_query_t *q2 = corm_query_new(db, &full_hook_model);
    corm_query_where(q2, "id = 1");
    corm_value_t new_name = { .type = CORM_STRING, .v.s = "Updated" };
    corm_query_set(q2, "name", new_name);
    int affected = 0;
    err = corm_update(q2, &affected);
    assert(err == CORM_OK);
    assert(before_update_called == true);
    assert(after_update_called == true);
    corm_query_free(q2);

    // Test delete hooks
    corm_query_t *q3 = corm_query_new(db, &full_hook_model);
    corm_query_where(q3, "id = 1");
    err = corm_delete(q3, &affected);
    assert(err == CORM_OK);
    assert(before_delete_called == true);
    assert(after_delete_called == true);
    corm_query_free(q3);

    corm_close(db);
    printf("test_all_lifecycle_hooks PASSED\n");
}

int main(void) {
    test_all_lifecycle_hooks();
    return 0;
}
