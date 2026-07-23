#include "corm/corm.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
  int id;
  char name[64];
  char deleted_at[32];
} SoftUser;

static corm_field_t soft_fields[] = {
    CORM_FIELD(SoftUser, id, CORM_INT, CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC,
               NULL),
    CORM_FIELD(SoftUser, name, CORM_STRING, 0, NULL),
    CORM_FIELD(SoftUser, deleted_at, CORM_STRING, CORM_FLAG_SOFT_DELETE, NULL),
};

static corm_model_t soft_user_model = {
    .table_name = "soft_users",
    .struct_size = sizeof(SoftUser),
    .fields = soft_fields,
    .field_count = 3,
    .primary_key = &soft_fields[0],
};

void test_soft_delete(void) {
  corm_t *db;
  corm_open("sqlite3://:memory:", &db);
  corm_register_model(db, &soft_user_model);
  corm_model_t *models[] = {&soft_user_model};
  corm_auto_migrate(db, models, 1);

  SoftUser u = {.name = "SoftAlice"};
  int64_t id = 0;
  corm_create_one(db, &soft_user_model, &u, &id);

  // Perform delete -> should turn into update deleted_at
  corm_query_t *q1 = corm_query_new(db, &soft_user_model);
  corm_query_where(q1, "id = 1");
  int affected = 0;
  assert(corm_delete(q1, &affected) == CORM_OK);
  corm_query_free(q1);

  // Query normal find -> should filter out soft-deleted row
  corm_query_t *q2 = corm_query_new(db, &soft_user_model);
  corm_result_t *res = NULL;
  corm_find(q2, &res);
  assert(res->row_count == 0);
  corm_result_release(res);
  corm_query_free(q2);

  // Query unscoped find -> should return row
  corm_query_t *q3 = corm_query_new(db, &soft_user_model);
  corm_query_unscoped(q3);
  corm_find(q3, &res);
  assert(res->row_count == 1);
  corm_result_release(res);
  corm_query_free(q3);

  corm_close(db);
  printf("test_soft_delete PASSED\n");
}

int main(void) {
  test_soft_delete();
  return 0;
}
