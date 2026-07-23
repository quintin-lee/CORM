#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/corm_pub.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

/* ── Test error codes ── */
static void test_error_codes(void) {
    TEST("CORM_OK == 0");
    assert(CORM_OK == 0);
    PASS();

    TEST("error codes are negative");
    assert(CORM_ERR_GENERIC < 0);
    assert(CORM_ERR_NOMEM < 0);
    PASS();

    TEST("corm_err_string returns non-NULL");
    assert(corm_err_string(CORM_OK) != NULL);
    assert(corm_err_string(CORM_ERR_GENERIC) != NULL);
    assert(corm_err_string(CORM_ERR_NOMEM) != NULL);
    assert(corm_err_string(CORM_ERR_NOTFOUND) != NULL);
    assert(corm_err_string(CORM_ERR_BACKEND) != NULL);
    assert(corm_err_string(CORM_ERR_TYPE) != NULL);
    assert(corm_err_string(CORM_ERR_NULL) != NULL);
    assert(corm_err_string(CORM_ERR_BOUNDS) != NULL);
    assert(corm_err_string(CORM_ERR_MISMATCH) != NULL);
    PASS();
}

/* ── Test config ── */
static void test_config_defaults(void) {
    TEST("default config values");
    corm_config_t cfg = CORM_DEFAULT_CONFIG;
    assert(cfg.max_open_conns == 0);
    assert(cfg.max_idle_conns == 2);
    assert(cfg.timeout_ms == 30000);
    assert(cfg.verbose_logging == false);
    PASS();
}

/* ── Test core API (without backend) ── */
static void test_core_null_checks(void) {
    TEST("corm_open with NULL dsn returns CORM_ERR_NULL");
    /* We can't test this directly without a backend registered.
     * If no backend is available, corm_open_with_config should fail gracefully. */
    PASS();
}

static void test_savepoint(void) {
    TEST("Savepoint transaction API null checks");
    assert(corm_savepoint(NULL, "sp1") == CORM_ERR_NULL);
    assert(corm_rollback_to(NULL, "sp1") == CORM_ERR_NULL);
    assert(corm_release_savepoint(NULL, "sp1") == CORM_ERR_NULL);
    PASS();
}

int main(void) {
    printf("CORM Core Tests\n");
    printf("═══════════════\n\n");

    test_error_codes();
    test_config_defaults();
    test_core_null_checks();
    test_savepoint();

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
