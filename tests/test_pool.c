#include "internal/pool.h"
#include <assert.h>
#include <stdio.h>

void test_connection_pool(void) {
    corm_config_t cfg = { .max_open_conns = 2, .max_idle_conns = 2, .timeout_ms = 5000 };
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

int main(void) {
    test_connection_pool();
    return 0;
}
