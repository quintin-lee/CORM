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

/* ── Test model descriptor ── */
typedef struct {
  int id;
  char name[256];
  int age;
  float score;
  bool active;
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
     .size = sizeof(int),
     .flags = 0,
     .default_value = "0"},
    {.name = "score",
     .type = CORM_FLOAT,
     .offset = offsetof(test_user_t, score),
     .size = sizeof(float)},
    {.name = "active",
     .type = CORM_BOOL,
     .offset = offsetof(test_user_t, active),
     .size = sizeof(bool)},
};

static corm_model_t test_user_model = {
    .table_name = "test_users",
    .struct_size = sizeof(test_user_t),
    .fields = test_user_fields,
    .field_count = 5,
    .primary_key = &test_user_fields[0],
};

static void test_model_descriptor(void) {
  TEST("model has correct table name");
  assert(strcmp(test_user_model.table_name, "test_users") == 0);
  PASS();

  TEST("model has correct field count");
  assert(test_user_model.field_count == 5);
  PASS();

  TEST("primary key is id field");
  assert(test_user_model.primary_key == &test_user_fields[0]);
  assert(strcmp(test_user_model.primary_key->name, "id") == 0);
  PASS();
}

static void test_field_find(void) {
  TEST("corm_find_field finds existing field");
  corm_field_t *f = corm_find_field(&test_user_model, "name");
  assert(f != NULL);
  assert(strcmp(f->name, "name") == 0);
  PASS();

  TEST("corm_find_field returns NULL for missing field");
  assert(corm_find_field(&test_user_model, "nonexistent") == NULL);
  PASS();
}

static void test_field_flags(void) {
  TEST("id field has PRIMARY flag");
  assert(test_user_fields[0].flags & CORM_FLAG_PRIMARY);
  PASS();

  TEST("id field has AUTOINC flag");
  assert(test_user_fields[0].flags & CORM_FLAG_AUTOINC);
  PASS();

  TEST("name field has NOT_NULL flag");
  assert(test_user_fields[1].flags & CORM_FLAG_NOT_NULL);
  PASS();

  TEST("age field has default value");
  assert(test_user_fields[2].default_value != NULL);
  assert(strcmp(test_user_fields[2].default_value, "0") == 0);
  PASS();
}

static void test_field_get_set(void) {
  TEST("corm_field_get_value reads int field");
  test_user_t user = {
      .id = 42, .name = "test", .age = 25, .score = 3.5f, .active = true};
  corm_value_t val = corm_field_get_value(&user, &test_user_fields[0]);
  assert(val.type == CORM_INT);
  assert(val.v.i == 42);
  PASS();

  TEST("corm_field_get_value reads string field");
  val = corm_field_get_value(&user, &test_user_fields[1]);
  assert(val.type == CORM_STRING);
  assert(strcmp(val.v.s, "test") == 0);
  PASS();

  TEST("corm_field_set_value writes int field");
  corm_value_t newval = {.type = CORM_INT, .v.i = 99};
  corm_field_set_value(&user, &test_user_fields[0], &newval);
  assert(user.id == 99);
  PASS();

  TEST("corm_field_set_value writes bool field");
  corm_value_t boolval = {.type = CORM_BOOL, .v.b = false};
  corm_field_set_value(&user, &test_user_fields[4], &boolval);
  assert(user.active == false);
  PASS();
}

static void test_model_associations(void) {
  TEST("corm_relation_t descriptor definition");
  corm_relation_t rel = {.name = "orders",
                         .type = CORM_REL_HAS_MANY,
                         .target_table = "orders",
                         .foreign_key = "user_id"};
  assert(strcmp(rel.name, "orders") == 0);
  assert(rel.type == CORM_REL_HAS_MANY);
  PASS();
}

int main(void) {
  printf("CORM Model Tests\n");
  printf("════════════════\n\n");

  test_model_descriptor();
  test_field_find();
  test_field_flags();
  test_field_get_set();
  test_model_associations();

  printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
  return tests_failed > 0 ? 1 : 0;
}
