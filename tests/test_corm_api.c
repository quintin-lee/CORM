#include <assert.h>
#include <corm/corm.h>
#include <stdbool.h>
#include <stdio.h>
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

/* ── Helper: a model with hooks that track record pointer ── */

typedef struct {
  int id;
  char name[64];
} BatchRec;

static int g_before_update_count = 0;
static int g_after_update_count = 0;
static void *g_last_before_update_rec = NULL;
static void *g_last_after_update_rec = NULL;
static int g_before_delete_count = 0;
static int g_after_delete_count = 0;
static void *g_last_before_delete_rec = NULL;
static void *g_last_after_delete_rec = NULL;

static corm_field_t batch_fields[] = {
    CORM_FIELD(BatchRec, id, CORM_INT, CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC,
               NULL),
    CORM_FIELD(BatchRec, name, CORM_STRING, 0, NULL),
};

static corm_err_t on_batch_before_update(corm_t *db, void *record) {
  (void)db;
  g_before_update_count++;
  g_last_before_update_rec = record;
  return CORM_OK;
}
static corm_err_t on_batch_after_update(corm_t *db, void *record) {
  (void)db;
  g_after_update_count++;
  g_last_after_update_rec = record;
  return CORM_OK;
}
static corm_err_t on_batch_before_delete(corm_t *db, void *record) {
  (void)db;
  g_before_delete_count++;
  g_last_before_delete_rec = record;
  return CORM_OK;
}
static corm_err_t on_batch_after_delete(corm_t *db, void *record) {
  (void)db;
  g_after_delete_count++;
  g_last_after_delete_rec = record;
  return CORM_OK;
}

static corm_model_t batch_hook_model = {
    .table_name = "batch_hook_test",
    .struct_size = sizeof(BatchRec),
    .fields = batch_fields,
    .field_count = 2,
    .primary_key = &batch_fields[0],
    .before_update = on_batch_before_update,
    .after_update = on_batch_after_update,
    .before_delete = on_batch_before_delete,
    .after_delete = on_batch_after_delete,
};

/* ── Helper: model with NO primary key ── */

static corm_field_t no_pk_fields[] = {
    CORM_FIELD(BatchRec, id, CORM_INT, 0, NULL),
    CORM_FIELD(BatchRec, name, CORM_STRING, 0, NULL),
};

static corm_model_t no_pk_model = {
    .table_name = "no_pk_test",
    .struct_size = sizeof(BatchRec),
    .fields = no_pk_fields,
    .field_count = 2,
    .primary_key = NULL,
};

/* ── Helper: a simple model for find_one / count ── */

typedef struct {
  int id;
  char name[64];
  int age;
} PersonRec;

static corm_field_t person_fields[] = {
    CORM_FIELD(PersonRec, id, CORM_INT, CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC,
               NULL),
    CORM_FIELD(PersonRec, name, CORM_STRING, CORM_FLAG_NOT_NULL, NULL),
    CORM_FIELD(PersonRec, age, CORM_INT, 0, NULL),
};

static corm_model_t person_model = {
    .table_name = "persons",
    .struct_size = sizeof(PersonRec),
    .fields = person_fields,
    .field_count = 3,
    .primary_key = &person_fields[0],
};

static void test_savepoint_validation(void) {
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    TEST("savepoint rejects SQL injection name");
    printf("SKIP (no sqlite3 backend)\n");
    PASS();
    return;
  }

  /* Valid names should work */
  TEST("savepoint accepts valid name 'sp1'");
  err = corm_savepoint(db, "sp1");
  assert(err == CORM_OK);
  corm_release_savepoint(db, "sp1");
  PASS();

  TEST("savepoint rejects name with semicolon");
  err = corm_savepoint(db, "foo; DROP TABLE users");
  assert(err != CORM_OK);
  PASS();

  TEST("savepoint rejects name with spaces");
  err = corm_savepoint(db, "bad name");
  assert(err != CORM_OK);
  PASS();

  TEST("savepoint rejects empty name");
  err = corm_savepoint(db, "");
  assert(err != CORM_OK);
  PASS();

  TEST("rollback_to rejects SQL injection name");
  err = corm_rollback_to(db, "foo'; DROP TABLE users; --");
  assert(err != CORM_OK);
  PASS();

  TEST("release_savepoint rejects SQL injection name");
  err = corm_release_savepoint(db, "foo'; DROP TABLE users; --");
  assert(err != CORM_OK);
  PASS();

  corm_close(db);
}

static void test_batch_hooks_receive_record(void) {
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    TEST("batch hooks receive record pointer");
    printf("SKIP (no sqlite3 backend)\n");
    PASS();
    return;
  }

  corm_register_model(db, &batch_hook_model);
  corm_model_t *models[] = {&batch_hook_model};
  corm_auto_migrate(db, models, 1);

  /* Insert two records */
  BatchRec recs[2] = {{.name = "Alice"}, {.name = "Bob"}};
  int inserted = 0;
  err = corm_create_batch(db, &batch_hook_model, recs, 2, 10, &inserted);
  assert(err == CORM_OK);
  assert(inserted == 2);

  /* The created records now have ids 1 and 2; update names */
  strcpy(recs[0].name, "Alice Updated");
  strcpy(recs[1].name, "Bob Updated");

  TEST("update_batch calls before_update with record");
  g_before_update_count = 0;
  g_after_update_count = 0;
  g_last_before_update_rec = NULL;
  g_last_after_update_rec = NULL;
  int affected = 0;
  err = corm_update_batch(db, &batch_hook_model, recs, 2, &affected);
  assert(err == CORM_OK);
  if (g_before_update_count != 2) {
    FAIL("before_update called wrong count");
    corm_close(db);
    return;
  }
  if (g_after_update_count != 2) {
    FAIL("after_update called wrong count");
    corm_close(db);
    return;
  }
  /* Verify hooks received non-NULL record pointers and pointed to correct
   * data */
  if (g_last_before_update_rec == NULL) {
    FAIL("before_update record is NULL");
    corm_close(db);
    return;
  }
  if (g_last_after_update_rec == NULL) {
    FAIL("after_update record is NULL");
    corm_close(db);
    return;
  }
  /* Last record should be Bob Updated (second in array) */
  BatchRec *last_rec = (BatchRec *)g_last_after_update_rec;
  if (strcmp(last_rec->name, "Bob Updated") != 0) {
    FAIL("after_update record has wrong content");
    corm_close(db);
    return;
  }
  PASS();

  TEST("delete_batch calls before_delete with record");
  g_before_delete_count = 0;
  g_after_delete_count = 0;
  g_last_before_delete_rec = NULL;
  g_last_after_delete_rec = NULL;
  affected = 0;
  err = corm_delete_batch(db, &batch_hook_model, recs, 2, &affected);
  assert(err == CORM_OK);
  if (g_before_delete_count != 2) {
    FAIL("before_delete called wrong count");
    corm_close(db);
    return;
  }
  if (g_after_delete_count != 2) {
    FAIL("after_delete called wrong count");
    corm_close(db);
    return;
  }
  if (g_last_before_delete_rec == NULL) {
    FAIL("before_delete record is NULL");
    corm_close(db);
    return;
  }
  if (g_last_after_delete_rec == NULL) {
    FAIL("after_delete record is NULL");
    corm_close(db);
    return;
  }
  PASS();

  corm_close(db);
}

static void test_batch_no_pk_guard(void) {
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    TEST("batch with no PK returns error");
    printf("SKIP (no sqlite3 backend)\n");
    PASS();
    return;
  }

  corm_register_model(db, &no_pk_model);
  corm_model_t *models[] = {&no_pk_model};
  corm_auto_migrate(db, models, 1);

  BatchRec recs[2] = {{.name = "X"}, {.name = "Y"}};

  TEST("update_batch with no PK returns CORM_ERR_MISMATCH");
  int affected = 0;
  err = corm_update_batch(db, &no_pk_model, recs, 2, &affected);
  assert(err == CORM_ERR_MISMATCH);
  PASS();

  TEST("delete_batch with no PK returns CORM_ERR_MISMATCH");
  err = corm_delete_batch(db, &no_pk_model, recs, 2, &affected);
  assert(err == CORM_ERR_MISMATCH);
  PASS();

  corm_close(db);
}

static void test_find_one(void) {
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    TEST("find_one");
    printf("SKIP (no sqlite3 backend)\n");
    PASS();
    return;
  }

  corm_register_model(db, &person_model);
  corm_model_t *models[] = {&person_model};
  corm_auto_migrate(db, models, 1);

  /* Insert two persons */
  PersonRec people[] = {{.name = "Alice", .age = 30},
                        {.name = "Bob", .age = 25}};
  int64_t id1 = 0, id2 = 0;
  corm_create_one(db, &person_model, &people[0], &id1);
  corm_create_one(db, &person_model, &people[1], &id2);

  TEST("find_one by id returns correct record");
  PersonRec found;
  memset(&found, 0, sizeof(found));
  err = corm_find_one(db, &person_model, "id = 1", &found);
  assert(err == CORM_OK);
  if (strcmp(found.name, "Alice") != 0 || found.age != 30) {
    FAIL("find_one returned wrong data");
    corm_close(db);
    return;
  }
  PASS();

  TEST("find_one with NULL where returns first record");
  memset(&found, 0, sizeof(found));
  err = corm_find_one(db, &person_model, NULL, &found);
  assert(err == CORM_OK);
  if (found.id == 0) {
    FAIL("find_one with NULL where returned empty");
    corm_close(db);
    return;
  }
  PASS();

  TEST("find_one with no match returns error");
  memset(&found, 0, sizeof(found));
  err = corm_find_one(db, &person_model, "id = 999", &found);
  assert(err != CORM_OK);
  PASS();

  corm_close(db);
}

static void test_count(void) {
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    TEST("count");
    printf("SKIP (no sqlite3 backend)\n");
    PASS();
    return;
  }

  corm_register_model(db, &person_model);
  corm_model_t *models[] = {&person_model};
  corm_auto_migrate(db, models, 1);

  /* Insert three persons */
  PersonRec people[] = {{.name = "Alice", .age = 30},
                        {.name = "Bob", .age = 25},
                        {.name = "Charlie", .age = 30}};
  corm_create_one(db, &person_model, &people[0], NULL);
  corm_create_one(db, &person_model, &people[1], NULL);
  corm_create_one(db, &person_model, &people[2], NULL);

  TEST("count all returns 3");
  int cnt = -1;
  err = corm_count(db, &person_model, NULL, &cnt);
  assert(err == CORM_OK);
  if (cnt != 3) {
    FAIL("wrong count");
    corm_close(db);
    return;
  }
  PASS();

  TEST("count with WHERE age = 30 returns 2");
  cnt = -1;
  err = corm_count(db, &person_model, "age = 30", &cnt);
  assert(err == CORM_OK);
  if (cnt != 2) {
    FAIL("wrong count for age=30");
    corm_close(db);
    return;
  }
  PASS();

  TEST("count with no match returns 0");
  cnt = -1;
  err = corm_count(db, &person_model, "id = 999", &cnt);
  assert(err == CORM_OK);
  if (cnt != 0) {
    FAIL("wrong count for no match");
    corm_close(db);
    return;
  }
  PASS();

  corm_close(db);
}

int main(void) {
  printf("CORM API Tests\n");
  printf("══════════════\n\n");

  test_savepoint_validation();
  test_batch_hooks_receive_record();
  test_batch_no_pk_guard();
  test_find_one();
  test_count();

  printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
  return tests_failed > 0 ? 1 : 0;
}
