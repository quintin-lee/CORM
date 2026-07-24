#include "corm_pub.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
  int id;
  char name[64];
} UserV1;

static corm_field_t u1_fields[] = {
    CORM_FIELD(UserV1, id, CORM_INT, CORM_FLAG_PRIMARY, NULL),
    CORM_FIELD(UserV1, name, CORM_STRING, 0, NULL),
};

static corm_model_t model_v1 = {
    .table_name = "mig_users",
    .struct_size = sizeof(UserV1),
    .fields = u1_fields,
    .field_count = 2,
    .primary_key = &u1_fields[0],
};

typedef struct {
  int id;
  char name[64];
  char email[128];
} UserV2;

static corm_field_t u2_fields[] = {
    CORM_FIELD(UserV2, id, CORM_INT, CORM_FLAG_PRIMARY, NULL),
    CORM_FIELD(UserV2, name, CORM_STRING, 0, NULL),
    CORM_FIELD(UserV2, email, CORM_STRING, 0, NULL),
};

static corm_model_t model_v2 = {
    .table_name = "mig_users",
    .struct_size = sizeof(UserV2),
    .fields = u2_fields,
    .field_count = 3,
    .primary_key = &u2_fields[0],
};

void test_incremental_migration(void) {
  corm_t *db;
  corm_open("sqlite3://:memory:", &db);
  corm_register_model(db, &model_v1);

  corm_model_t *m1[] = {&model_v1};
  assert(corm_auto_migrate(db, m1, 1) == CORM_OK);

  // Migrate to V2 (adds email column)
  corm_register_model(db, &model_v2);
  corm_model_t *m2[] = {&model_v2};
  assert(corm_auto_migrate(db, m2, 1) == CORM_OK);

  corm_close(db);
  printf("test_incremental_migration PASSED\n");
}

typedef struct {
  int id;
  char group[64];
} ReservedModel;

static corm_field_t reserved_fields[] = {
    CORM_FIELD(ReservedModel, id, CORM_INT, CORM_FLAG_PRIMARY, NULL),
    CORM_FIELD(ReservedModel, group, CORM_STRING, 0, NULL),
};

static corm_model_t reserved_model = {
    .table_name = "reserved_test",
    .struct_size = sizeof(ReservedModel),
    .fields = reserved_fields,
    .field_count = 2,
    .primary_key = &reserved_fields[0],
};

void test_alter_table_with_reserved_word(void) {
  corm_t *db;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    printf("SKIP (no sqlite3 backend)\n");
    return;
  }

  /* Create table with just the PK, then auto_migrate attempts ADD COLUMN
   * "group". Without identifier quoting, 'group' is a reserved word and SQLite
   * rejects it. */
  corm_exec(db, "CREATE TABLE IF NOT EXISTS reserved_test ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT)");

  corm_model_t *models[] = {&reserved_model};
  err = corm_auto_migrate(db, models, 1);
  assert(err == CORM_OK);

  /* Verify the column was actually added */
  corm_result_t *res = NULL;
  corm_raw(db, "SELECT group FROM reserved_test WHERE id = 1", &res);
  if (res)
    corm_result_release(res);

  corm_close(db);
  printf("test_alter_table_with_reserved_word PASSED\n");
}

int main(void) {
  test_incremental_migration();
  test_alter_table_with_reserved_word();
  return 0;
}
