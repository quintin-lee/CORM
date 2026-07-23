#include "corm/corm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool logger_called = false;

static void my_logger(corm_log_level_t level, const char *sql,
                      uint64_t elapsed_us, void *user_data) {
  (void)level;
  (void)elapsed_us;
  (void)user_data;
  if (sql && strstr(sql, "SELECT 1")) {
    logger_called = true;
  }
}

void test_logger_interceptor(void) {
  corm_t *db;
  corm_open("sqlite3://:memory:", &db);
  corm_set_logger(db, my_logger, NULL);

  corm_exec(db, "SELECT 1");
  assert(logger_called == true);

  corm_close(db);
  printf("test_logger_interceptor PASSED\n");
}

int main(void) {
  test_logger_interceptor();
  return 0;
}
