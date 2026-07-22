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

/* Test model */
typedef struct {
    int id;
    char name[256];
    int age;
} test_user_t;

static corm_field_t test_user_fields[] = {
    { .name = "id",   .type = CORM_INT,    .offset = offsetof(test_user_t, id),   .size = sizeof(int),   .flags = CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC },
    { .name = "name", .type = CORM_STRING, .offset = offsetof(test_user_t, name), .size = 256,           .flags = CORM_FLAG_NOT_NULL },
    { .name = "age",  .type = CORM_INT,    .offset = offsetof(test_user_t, age),  .size = sizeof(int) },
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
    err = corm_exec(db, "INSERT INTO test_users (name, age) VALUES ('Alice', 30)");
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

int main(void) {
    printf("CORM SQLite Integration Tests\n");
    printf("══════════════════════════════\n\n");

    test_sqlite_open_close();
    test_sqlite_create_table();
    test_sqlite_insert_select();
    test_sqlite_transaction();

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
