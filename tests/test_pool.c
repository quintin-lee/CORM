#include "internal/corm_internal.h"
#include "internal/pool.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

void test_connection_pool(void) {
  corm_config_t cfg = {
      .max_open_conns = 2, .max_idle_conns = 2, .timeout_ms = 5000};
  corm_pool_t *pool = NULL;
  corm_err_t err = corm_pool_create("sqlite3://:memory:", cfg, &pool);
  assert(err == CORM_OK);

  corm_t *db1 = NULL, *db2 = NULL;
  assert(corm_pool_acquire(pool, &db1) == CORM_OK);
  assert(corm_pool_acquire(pool, &db2) == CORM_OK);

  assert(corm_pool_release(pool, db1) == CORM_OK);
  assert(corm_pool_release(pool, db2) == CORM_OK);

  corm_pool_destroy(pool);
  printf("test_connection_pool PASSED\n");
}

void test_pool_timeout(void) {
  corm_config_t cfg = {
      .max_open_conns = 1, .max_idle_conns = 1, .timeout_ms = 100};
  corm_pool_t *pool = NULL;
  assert(corm_pool_create("sqlite3://:memory:", cfg, &pool) == CORM_OK);

  corm_t *db1 = NULL, *db2 = NULL;
  assert(corm_pool_acquire(pool, &db1) == CORM_OK);

  /* Attempt acquire when max_open reached and expect timeout failure */
  corm_err_t err = corm_pool_acquire(pool, &db2);
  assert(err != CORM_OK);

  assert(corm_pool_release(pool, db1) == CORM_OK);
  corm_pool_destroy(pool);
  printf("test_pool_timeout PASSED\n");
}

void test_pool_idle_limit(void) {
  corm_config_t cfg = {.max_open_conns = 0,
                       .max_idle_conns = 2,
                       .conn_max_lifetime_ms = 0,
                       .timeout_ms = 5000};
  corm_pool_t *pool = NULL;
  assert(corm_pool_create("sqlite3://:memory:", cfg, &pool) == CORM_OK);

  corm_t *dbs[5];
  for (int i = 0; i < 5; i++)
    assert(corm_pool_acquire(pool, &dbs[i]) == CORM_OK);

  /* Release all 5 — only 2 should remain idle (idle_count == 2) */
  for (int i = 0; i < 5; i++)
    assert(corm_pool_release(pool, dbs[i]) == CORM_OK);

  assert(pool->idle_count == 2);
  assert(pool->current_open == 2);

  corm_pool_destroy(pool);
  printf("test_pool_idle_limit PASSED\n");
}

void test_pool_conn_max_lifetime(void) {
  corm_config_t cfg = {.max_open_conns = 0,
                       .max_idle_conns = 2,
                       .conn_max_lifetime_ms = 50,
                       .timeout_ms = 5000};
  corm_pool_t *pool = NULL;
  assert(corm_pool_create("sqlite3://:memory:", cfg, &pool) == CORM_OK);

  corm_t *db = NULL;
  assert(corm_pool_acquire(pool, &db) == CORM_OK);
  int64_t created_before = db->created_at_ms;

  /* Sleep longer than conn_max_lifetime_ms */
  struct timespec ts = {.tv_sec = 0, .tv_nsec = 60000000};
  nanosleep(&ts, NULL);

  assert(corm_pool_release(pool, db) == CORM_OK);

  /* Re-acquire — should get a NEW connection (old one expired) */
  assert(corm_pool_acquire(pool, &db) == CORM_OK);
  assert(db->created_at_ms > created_before);

  assert(corm_pool_release(pool, db) == CORM_OK);
  corm_pool_destroy(pool);
  printf("test_pool_conn_max_lifetime PASSED\n");
}

int main(void) {
  test_connection_pool();
  test_pool_timeout();
  test_pool_idle_limit();
  test_pool_conn_max_lifetime();
  return 0;
}
