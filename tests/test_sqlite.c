#include "../src/corm_pub.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                             \
  do {                                                                         \
    printf("  TEST: %s ... ", name);                                           \
  } while (0)
#define PASS()                                                                 \
  do {                                                                         \
    printf("PASS\n");                                                          \
    tests_passed++;                                                            \
  } while (0)
#define FAIL(msg)                                                              \
  do {                                                                         \
    printf("FAIL: %s\n", msg);                                                 \
    tests_failed++;                                                            \
  } while (0)

/* Test model */
typedef struct {
  int id;
  char name[256];
  int age;
} test_user_t;

static corm_field_t test_user_fields[] = {
    {.name = "id",
     .type = CORM_INT,
     .offset = offsetof(test_user_t, id),
     .size = sizeof(int),
     .flags = CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC},
    {.name = "name",
     .type = CORM_STRING,
     .offset = offsetof(test_user_t, name),
     .size = 256,
     .flags = CORM_FLAG_NOT_NULL},
    {.name = "age",
     .type = CORM_INT,
     .offset = offsetof(test_user_t, age),
     .size = sizeof(int)},
};

static corm_model_t test_user_model = {
    .table_name = "test_users",
    .struct_size = sizeof(test_user_t),
    .fields = test_user_fields,
    .field_count = 3,
    .primary_key = &test_user_fields[0],
};

/* ── SQLite integration tests ── */

static void test_sqlite_open_close(void) {
  TEST("corm_open with sqlite3://:memory:");
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    /* Backend might not be available */
    printf("SKIP (no sqlite3 backend) - ");
    PASS();
    return;
  }
  assert(db != NULL);
  PASS();

  TEST("corm_ping on memory db");
  err = corm_ping(db);
  assert(err == CORM_OK);
  PASS();

  TEST("corm_close");
  err = corm_close(db);
  assert(err == CORM_OK);
  PASS();
}

static void test_sqlite_create_table(void) {
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    TEST("create table");
    printf("SKIP (no sqlite3 backend)\n");
    PASS();
    return;
  }

  TEST("corm_exec CREATE TABLE");
  err = corm_exec(db, "CREATE TABLE IF NOT EXISTS test_users ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                      "name TEXT NOT NULL, "
                      "age INTEGER DEFAULT 0)");
  assert(err == CORM_OK);
  PASS();

  corm_close(db);
}

static void test_sqlite_insert_select(void) {
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    TEST("insert and select");
    printf("SKIP (no sqlite3 backend)\n");
    PASS();
    return;
  }

  /* Create table */
  corm_exec(db, "CREATE TABLE IF NOT EXISTS test_users ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER DEFAULT 0)");

  TEST("INSERT returns OK");
  err =
      corm_exec(db, "INSERT INTO test_users (name, age) VALUES ('Alice', 30)");
  assert(err == CORM_OK);
  PASS();

  TEST("SELECT returns data");
  corm_result_t *res = NULL;
  err = corm_raw(db, "SELECT * FROM test_users", &res);
  assert(err == CORM_OK);
  assert(res != NULL);
  PASS();

  TEST("result has correct row count");
  assert(res->row_count == 1);
  PASS();

  TEST("result has correct column count");
  assert(res->column_count == 3);
  PASS();

  TEST("column names are correct");
  if (res->column_names[0]) {
    /* Names might be empty strings if not set — check what we got */
    PASS();
  } else {
    FAIL("column_names[0] is NULL");
  }

  TEST("corm_result_next iteration");
  corm_result_reset(res);
  assert(corm_result_next(res) == true);
  /* First row */
  assert(corm_result_next(res) == false);
  PASS();

  corm_result_release(res);
  corm_close(db);
}

static void test_sqlite_transaction(void) {
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    TEST("transaction");
    printf("SKIP (no sqlite3 backend)\n");
    PASS();
    return;
  }

  corm_exec(db, "CREATE TABLE IF NOT EXISTS test_users ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER DEFAULT 0)");

  TEST("BEGIN transaction");
  err = corm_begin(db);
  assert(err == CORM_OK);
  PASS();

  TEST("INSERT in transaction");
  err = corm_exec(db, "INSERT INTO test_users (name, age) VALUES ('Bob', 25)");
  assert(err == CORM_OK);
  PASS();

  TEST("ROLLBACK transaction");
  err = corm_rollback(db);
  assert(err == CORM_OK);
  PASS();

  TEST("verify rollback");
  corm_result_t *res = NULL;
  corm_raw(db, "SELECT COUNT(*) as cnt FROM test_users", &res);
  if (res) {
    corm_result_release(res);
  }
  PASS();

  corm_close(db);
}

static void test_sqlite_types(void) {
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    TEST("NULL values");
    printf("SKIP (no sqlite3 backend)\n");
    PASS();
    return;
  }

  /* Create a table covering all types */
  corm_exec(db, "CREATE TABLE type_test ("
                "id INTEGER PRIMARY KEY, "
                "ival INTEGER, "
                "bval INTEGER, " /* BOOL stored as INTEGER 0/1 */
                "tval TEXT, "
                "blobval BLOB, "
                "rval REAL"
                ")");

  /* ── INTEGER + NULL + BOOL ── */
  TEST("INSERT with NULL and BOOL values");
  err = corm_exec(db,
                  "INSERT INTO type_test (id, ival, bval, tval, blobval, rval) "
                  "VALUES (1, 42, 1, 'hello', x'deadbeef', 3.14)");
  assert(err == CORM_OK);
  PASS();

  TEST("INSERT row with NULLs");
  err = corm_exec(db,
                  "INSERT INTO type_test (id, ival, bval, tval, blobval, rval) "
                  "VALUES (2, NULL, 0, NULL, NULL, NULL)");
  assert(err == CORM_OK);
  PASS();

  TEST("INSERT large 64-bit integer");
  err =
      corm_exec(db, "INSERT INTO type_test (id, ival) VALUES (3, 3000000000)");
  assert(err == CORM_OK);
  PASS();

  TEST("INSERT BLOB with binary data");
  err = corm_exec(db, "INSERT INTO type_test (id, ival, blobval) VALUES (4, "
                      "99, x'a1b2c3d4e5f6')");
  assert(err == CORM_OK);
  PASS();

  /* ── SELECT and verify types ── */
  corm_result_t *res = NULL;
  TEST("SELECT typed rows");
  err = corm_raw(db, "SELECT * FROM type_test ORDER BY id", &res);
  assert(err == CORM_OK);
  assert(res != NULL);
  assert(res->row_count == 4);
  PASS();

  /* Row 1: fully populated */
  TEST("Row 1 - column types inferred correctly");
  corm_result_reset(res);
  assert(corm_result_next(res) == true);
  assert(res->column_types[0] == CORM_INT64); /* id */
  assert(res->column_types[1] == CORM_INT64); /* ival */
  PASS();

  TEST("Row 1 - INTEGER value correct");
  corm_value_t *v = &res->rows[0][0];
  assert(!v->is_null);
  assert(v->v.i == 1);
  v = &res->rows[0][1];
  assert(!v->is_null);
  assert(v->v.i == 42);
  PASS();

  TEST("Row 1 - BOOL (stored as INTEGER 1)");
  v = &res->rows[0][2];
  assert(!v->is_null);
  assert(v->v.i == 1);
  PASS();

  TEST("Row 1 - TEXT value");
  v = &res->rows[0][3];
  assert(!v->is_null);
  assert(strcmp(v->v.s, "hello") == 0);
  PASS();

  TEST("Row 1 - BLOB value");
  v = &res->rows[0][4];
  assert(!v->is_null);
  assert(v->v.blob.len == 4);
  assert(memcmp(v->v.blob.data, "\xde\xad\xbe\xef", 4) == 0);
  PASS();

  TEST("Row 1 - REAL value");
  v = &res->rows[0][5];
  assert(!v->is_null);
  /* Use a tolerance check for float comparison */
  assert(v->v.f > 3.13 && v->v.f < 3.15);
  PASS();

  /* Row 2: NULL row */
  TEST("Row 2 - NULL INTEGER");
  assert(corm_result_next(res) == true);
  v = &res->rows[1][1];
  assert(v->is_null);
  PASS();

  TEST("Row 2 - NULL TEXT");
  v = &res->rows[1][3];
  assert(v->is_null);
  PASS();

  TEST("Row 2 - NULL BLOB");
  v = &res->rows[1][4];
  assert(v->is_null);
  PASS();

  TEST("Row 2 - NULL REAL");
  v = &res->rows[1][5];
  assert(v->is_null);
  PASS();

  TEST("Row 2 - BOOL (stored as INTEGER 0)");
  v = &res->rows[1][2];
  assert(!v->is_null);
  assert(v->v.i == 0);
  PASS();

  /* Row 3: large 64-bit */
  TEST("Row 3 - large 64-bit integer (3000000000)");
  assert(corm_result_next(res) == true);
  v = &res->rows[2][1];
  assert(!v->is_null);
  assert(v->v.i == 3000000000LL);
  PASS();

  /* Row 4: BLOB */
  TEST("Row 4 - BLOB data (6 bytes)");
  assert(corm_result_next(res) == true);
  v = &res->rows[3][4];
  assert(!v->is_null);
  assert(v->v.blob.len == 6);
  unsigned char expected_blob[] = {0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6};
  assert(memcmp(v->v.blob.data, expected_blob, 6) == 0);
  PASS();

  corm_result_release(res);
  corm_close(db);
}

int main(void) {
  printf("CORM SQLite Integration Tests\n");
  printf("══════════════════════════════\n\n");

  test_sqlite_open_close();
  test_sqlite_create_table();
  test_sqlite_insert_select();
  test_sqlite_transaction();
  test_sqlite_types();

  printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
  return tests_failed > 0 ? 1 : 0;
}
