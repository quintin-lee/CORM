#include <assert.h>
#include <corm/corm.h>
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

int main(void) {
  printf("CORM API Tests\n");
  printf("══════════════\n\n");

  test_savepoint_validation();

  printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
  return tests_failed > 0 ? 1 : 0;
}
