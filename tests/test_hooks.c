#include "corm_pub.h"
#include <assert.h>
#include <stdio.h>

static bool hook_before_create_called = false;
static bool hook_after_create_called = false;

typedef struct {
  int id;
  char name[64];
} HookUser;

static corm_field_t hook_user_fields[] = {
    CORM_FIELD(HookUser, id, CORM_INT, CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC,
               NULL),
    CORM_FIELD(HookUser, name, CORM_STRING, 0, NULL),
};

static corm_err_t on_before_create(corm_t *db, void *record) {
  (void)db;
  (void)record;
  hook_before_create_called = true;
  return CORM_OK;
}

static corm_err_t on_after_create(corm_t *db, void *record) {
  (void)db;
  (void)record;
  hook_after_create_called = true;
  return CORM_OK;
}

static corm_model_t hook_user_model = {
    .table_name = "hook_users",
    .struct_size = sizeof(HookUser),
    .fields = hook_user_fields,
    .field_count = 2,
    .primary_key = &hook_user_fields[0],
    .before_create = on_before_create,
    .after_create = on_after_create,
};

void test_model_hooks(void) {
  corm_t *db;
  corm_open("sqlite3://:memory:", &db);
  corm_register_model(db, &hook_user_model);
  corm_model_t *models[] = {&hook_user_model};
  corm_auto_migrate(db, models, 1);

  HookUser user = {.name = "HookTester"};
  int64_t id = 0;
  corm_err_t err = corm_create_one(db, &hook_user_model, &user, &id);
  assert(err == CORM_OK);
  assert(hook_before_create_called == true);
  assert(hook_after_create_called == true);

  corm_close(db);
  printf("test_model_hooks PASSED\n");
}

int main(void) {
  test_model_hooks();
  return 0;
}
