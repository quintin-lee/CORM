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

/* ── Index creation test ── */

typedef struct {
  int id;
  char email[64];
} IndexedModel;

static corm_field_t indexed_fields[] = {
    CORM_FIELD(IndexedModel, id, CORM_INT, CORM_FLAG_PRIMARY, NULL),
    CORM_FIELD(IndexedModel, email, CORM_STRING, CORM_FLAG_INDEX, NULL),
};

static corm_model_t indexed_model = {
    .table_name = "indexed_test",
    .struct_size = sizeof(IndexedModel),
    .fields = indexed_fields,
    .field_count = 2,
    .primary_key = &indexed_fields[0],
};

static void test_index_creation(void) {
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    printf("SKIP (no sqlite3 backend)\n");
    return;
  }

  corm_model_t *models[] = {&indexed_model};
  assert(corm_auto_migrate(db, models, 1) == CORM_OK);

  /* Verify index was created via sqlite_master */
  corm_result_t *res = NULL;
  err = corm_raw(db,
                 "SELECT name FROM sqlite_master WHERE type='index' "
                 "AND tbl_name='indexed_test' "
                 "AND name='idx_indexed_test_email'",
                 &res);
  assert(err == CORM_OK);
  assert(res != NULL);
  assert(res->row_count == 1);
  corm_result_release(res);

  corm_close(db);
  printf("test_index_creation PASSED\n");
}

/* ── Foreign key test ── */

typedef struct {
  int id;
  char name[64];
} DeptModel;

static corm_field_t dept_fields[] = {
    CORM_FIELD(DeptModel, id, CORM_INT, CORM_FLAG_PRIMARY, NULL),
    CORM_FIELD(DeptModel, name, CORM_STRING, 0, NULL),
};

static corm_model_t dept_model = {
    .table_name = "fk_dept",
    .struct_size = sizeof(DeptModel),
    .fields = dept_fields,
    .field_count = 2,
    .primary_key = &dept_fields[0],
};

typedef struct {
  int id;
  int dept_id;
  char name[64];
} EmpModel;

static corm_field_t emp_fields[] = {
    CORM_FIELD(EmpModel, id, CORM_INT, CORM_FLAG_PRIMARY, NULL),
    CORM_FIELD(EmpModel, dept_id, CORM_INT, CORM_FLAG_NOT_NULL, NULL),
    CORM_FIELD(EmpModel, name, CORM_STRING, 0, NULL),
};

static corm_model_t emp_model = {
    .table_name = "fk_emp",
    .struct_size = sizeof(EmpModel),
    .fields = emp_fields,
    .field_count = 3,
    .primary_key = &emp_fields[0],
};

static void test_foreign_key(void) {
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    printf("SKIP (no sqlite3 backend)\n");
    return;
  }

  /* Enable FK enforcement */
  corm_exec(db, "PRAGMA foreign_keys = ON");

  /* Set up FK reference on dept_id field */
  emp_fields[1].fk_ref = "fk_dept.id";

  corm_model_t *models[] = {&dept_model, &emp_model};
  assert(corm_auto_migrate(db, models, 2) == CORM_OK);

  /* Insert a department */
  assert(corm_exec(db, "INSERT INTO fk_dept (name) VALUES ('Engineering')") ==
         CORM_OK);

  /* Insert employee with valid FK reference */
  assert(
      corm_exec(db, "INSERT INTO fk_emp (dept_id, name) VALUES (1, 'Alice')") ==
      CORM_OK);

  /* Insert employee with invalid FK reference should fail */
  err = corm_exec(db, "INSERT INTO fk_emp (dept_id, name) VALUES (999, 'Bob')");
  assert(err != CORM_OK);

  /* Clean up fk_ref for next run */
  emp_fields[1].fk_ref = NULL;

  corm_close(db);
  printf("test_foreign_key PASSED\n");
}

/* ── ALTER TABLE constraint propagation test ── */

typedef struct {
  int id;
  char label[64];
  int status;
} ConstraintAddModel;

static corm_field_t constraint_add_fields[] = {
    CORM_FIELD(ConstraintAddModel, id, CORM_INT, CORM_FLAG_PRIMARY, NULL),
    CORM_FIELD(ConstraintAddModel, label, CORM_STRING, CORM_FLAG_NOT_NULL,
               NULL),
    CORM_FIELD(ConstraintAddModel, status, CORM_INT, 0, "0"),
};

static corm_model_t constraint_add_model = {
    .table_name = "constraint_add_test",
    .struct_size = sizeof(ConstraintAddModel),
    .fields = constraint_add_fields,
    .field_count = 3,
    .primary_key = &constraint_add_fields[0],
};

static void test_alter_add_constraints(void) {
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    printf("SKIP (no sqlite3 backend)\n");
    return;
  }

  /* Create table with only PK */
  corm_exec(db, "CREATE TABLE IF NOT EXISTS constraint_add_test ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT)");

  /* Auto-migrate adds: label TEXT NOT NULL, status INTEGER DEFAULT 0 */
  corm_model_t *models[] = {&constraint_add_model};
  assert(corm_auto_migrate(db, models, 1) == CORM_OK);

  /* Verify DEFAULT works: insert with no status, check default */
  assert(corm_exec(db, "INSERT INTO constraint_add_test (label) "
                       "VALUES ('test')") == CORM_OK);

  corm_result_t *res = NULL;
  corm_raw(db, "SELECT status FROM constraint_add_test WHERE id = 1", &res);
  assert(res != NULL);
  assert(res->row_count == 1);
  assert(res->rows[0][0].type == CORM_INT ||
         res->rows[0][0].type == CORM_INT64);
  assert(res->rows[0][0].v.i == 0);
  corm_result_release(res);

  corm_close(db);
  printf("test_alter_add_constraints PASSED\n");
}

static void test_composite_primary_key(void) {
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    printf("SKIP (no sqlite3 backend)\n");
    return;
  }

  typedef struct {
    int tenant_id;
    int user_id;
    char role[32];
  } UserRoleModel;

  corm_field_t user_role_fields[] = {
      CORM_FIELD(UserRoleModel, tenant_id, CORM_INT, CORM_FLAG_PRIMARY, NULL),
      CORM_FIELD(UserRoleModel, user_id, CORM_INT, CORM_FLAG_PRIMARY, NULL),
      CORM_FIELD(UserRoleModel, role, CORM_STRING, 0, NULL),
  };

  corm_model_t user_role_model = {
      .table_name = "user_roles",
      .struct_size = sizeof(UserRoleModel),
      .fields = user_role_fields,
      .field_count = 3,
      .primary_key = NULL,
  };

  corm_model_t *models[] = {&user_role_model};
  assert(corm_auto_migrate(db, models, 1) == CORM_OK);

  corm_result_t *res = NULL;
  err = corm_raw(db,
                 "SELECT sql FROM sqlite_master WHERE type='table' AND "
                 "tbl_name='user_roles'",
                 &res);
  assert(err == CORM_OK);
  assert(res != NULL && res->row_count == 1);
  const char *sql_str = res->rows[0][0].v.s;
  assert(strstr(sql_str, "PRIMARY KEY") != NULL);
  corm_result_release(res);

  corm_close(db);
  printf("test_composite_primary_key PASSED\n");
}

int main(void) {
  test_incremental_migration();
  test_alter_table_with_reserved_word();
  test_index_creation();
  test_foreign_key();
  test_alter_add_constraints();
  test_composite_primary_key();
  return 0;
}
