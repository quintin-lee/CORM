#include "corm/corm.h"
#include "internal/stmt_cache.h"
#include <assert.h>
#include <stdio.h>

void test_statement_cache(void) {
  corm_stmt_cache_t *cache = NULL;
  corm_err_t err = corm_stmt_cache_create(2, &cache);
  assert(err == CORM_OK);

  assert(corm_stmt_cache_put(cache, "SELECT 1", (void *)0x1000) == CORM_OK);
  assert(corm_stmt_cache_put(cache, "SELECT 2", (void *)0x2000) == CORM_OK);

  assert(corm_stmt_cache_get(cache, "SELECT 1") == (void *)0x1000);
  assert(corm_stmt_cache_get(cache, "SELECT 2") == (void *)0x2000);

  // Test LRU eviction when capacity 2 is exceeded
  assert(corm_stmt_cache_put(cache, "SELECT 3", (void *)0x3000) == CORM_OK);
  assert(corm_stmt_cache_get(cache, "SELECT 3") == (void *)0x3000);

  corm_stmt_cache_destroy(cache);
  printf("test_statement_cache PASSED\n");
}

int main(void) {
  test_statement_cache();
  return 0;
}
