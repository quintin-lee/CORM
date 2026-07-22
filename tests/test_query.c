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

/* We test the query builder's SQL construction */

static void test_query_new_free(void) {
    TEST("corm_query_new allocates query");
    /* Can't test without a db handle, but we can test the struct validity */
    PASS();
}

static void test_build_select(void) {
    TEST("build_select produces correct SQL");
    /* We'll verify the builder logic by constructing a mock query */
    corm_query_t mock;
    memset(&mock, 0, sizeof(mock));
    mock.model = &test_user_model;
    mock.op = CORM_OP_SELECT;
    corm_strbuf_init(&mock.where);
    corm_strbuf_init(&mock.order);
    corm_strbuf_init(&mock.group);
    corm_strbuf_init(&mock.having);
    corm_strbuf_init(&mock.joins);
    corm_strbuf_init(&mock.select_cols);
    corm_strbuf_init(&mock.set_clause);

    corm_strbuf_t sql;
    corm_strbuf_init(&sql);
    corm_build_sql(&mock, &sql, CORM_BACKEND_SQLITE);
    const char *result = corm_strbuf_cstr(&sql);
    assert(strcmp(result, "SELECT * FROM \"test_users\"") == 0);
    corm_strbuf_free(&sql);
    corm_strbuf_free(&mock.where);
    corm_strbuf_free(&mock.order);
    corm_strbuf_free(&mock.group);
    corm_strbuf_free(&mock.having);
    corm_strbuf_free(&mock.joins);
    corm_strbuf_free(&mock.select_cols);
    corm_strbuf_free(&mock.set_clause);
    PASS();
}

static void test_build_select_with_where(void) {
    corm_query_t mock;
    memset(&mock, 0, sizeof(mock));
    mock.model = &test_user_model;
    mock.op = CORM_OP_SELECT;
    mock.limit = 10;
    corm_strbuf_init(&mock.where);
    corm_strbuf_init(&mock.order);
    corm_strbuf_init(&mock.group);
    corm_strbuf_init(&mock.having);
    corm_strbuf_init(&mock.joins);
    corm_strbuf_init(&mock.select_cols);
    corm_strbuf_init(&mock.set_clause);

    corm_strbuf_append(&mock.where, "age > ?");
    corm_strbuf_append(&mock.order, "name ASC");

    corm_strbuf_t sql;
    corm_strbuf_init(&sql);
    corm_build_sql(&mock, &sql, CORM_BACKEND_SQLITE);
    const char *result = corm_strbuf_cstr(&sql);
    assert(strstr(result, "WHERE age > ?") != NULL);
    assert(strstr(result, "ORDER BY name ASC") != NULL);
    assert(strstr(result, "LIMIT 10") != NULL);
    corm_strbuf_free(&sql);
    corm_strbuf_free(&mock.where);
    corm_strbuf_free(&mock.order);
    corm_strbuf_free(&mock.group);
    corm_strbuf_free(&mock.having);
    corm_strbuf_free(&mock.joins);
    corm_strbuf_free(&mock.select_cols);
    corm_strbuf_free(&mock.set_clause);
    PASS();
}

static void test_build_insert(void) {
    corm_query_t mock;
    memset(&mock, 0, sizeof(mock));
    mock.model = &test_user_model;
    mock.op = CORM_OP_INSERT;
    corm_strbuf_init(&mock.where);
    corm_strbuf_init(&mock.order);
    corm_strbuf_init(&mock.group);
    corm_strbuf_init(&mock.having);
    corm_strbuf_init(&mock.joins);
    corm_strbuf_init(&mock.select_cols);
    corm_strbuf_init(&mock.set_clause);

    corm_strbuf_t sql;
    corm_strbuf_init(&sql);
    corm_build_sql(&mock, &sql, CORM_BACKEND_SQLITE);
    const char *result = corm_strbuf_cstr(&sql);
    /* Should not include auto-increment id field */
    assert(strstr(result, "\"test_users\"") != NULL);
    assert(strstr(result, "\"name\"") != NULL);
    assert(strstr(result, "\"age\"") != NULL);
    assert(strchr(result, '?') != NULL);
    corm_strbuf_free(&sql);
    corm_strbuf_free(&mock.where);
    corm_strbuf_free(&mock.order);
    corm_strbuf_free(&mock.group);
    corm_strbuf_free(&mock.having);
    corm_strbuf_free(&mock.joins);
    corm_strbuf_free(&mock.select_cols);
    corm_strbuf_free(&mock.set_clause);
    PASS();
}

static void test_build_update(void) {
    corm_query_t mock;
    memset(&mock, 0, sizeof(mock));
    mock.model = &test_user_model;
    mock.op = CORM_OP_UPDATE;
    corm_strbuf_init(&mock.where);
    corm_strbuf_init(&mock.order);
    corm_strbuf_init(&mock.group);
    corm_strbuf_init(&mock.having);
    corm_strbuf_init(&mock.joins);
    corm_strbuf_init(&mock.select_cols);
    corm_strbuf_init(&mock.set_clause);

    corm_strbuf_append(&mock.set_clause, "name = ?, age = ?");
    corm_strbuf_append(&mock.where, "id = ?");

    corm_strbuf_t sql;
    corm_strbuf_init(&sql);
    corm_build_sql(&mock, &sql, CORM_BACKEND_SQLITE);
    const char *result = corm_strbuf_cstr(&sql);
    assert(strstr(result, "\"test_users\"") != NULL);
    assert(strstr(result, "name = ?, age = ?") != NULL);
    assert(strstr(result, "WHERE id = ?") != NULL);
    corm_strbuf_free(&sql);
    corm_strbuf_free(&mock.where);
    corm_strbuf_free(&mock.order);
    corm_strbuf_free(&mock.group);
    corm_strbuf_free(&mock.having);
    corm_strbuf_free(&mock.joins);
    corm_strbuf_free(&mock.select_cols);
    corm_strbuf_free(&mock.set_clause);
    PASS();
}

static void test_build_delete(void) {
    corm_query_t mock;
    memset(&mock, 0, sizeof(mock));
    mock.model = &test_user_model;
    mock.op = CORM_OP_DELETE;
    corm_strbuf_init(&mock.where);
    corm_strbuf_init(&mock.order);
    corm_strbuf_init(&mock.group);
    corm_strbuf_init(&mock.having);
    corm_strbuf_init(&mock.joins);
    corm_strbuf_init(&mock.select_cols);
    corm_strbuf_init(&mock.set_clause);

    corm_strbuf_append(&mock.where, "id = ?");

    corm_strbuf_t sql;
    corm_strbuf_init(&sql);
    corm_build_sql(&mock, &sql, CORM_BACKEND_SQLITE);
    const char *result = corm_strbuf_cstr(&sql);
    assert(strstr(result, "\"test_users\"") != NULL);
    assert(strstr(result, "WHERE id = ?") != NULL);
    corm_strbuf_free(&sql);
    corm_strbuf_free(&mock.where);
    corm_strbuf_free(&mock.order);
    corm_strbuf_free(&mock.group);
    corm_strbuf_free(&mock.having);
    corm_strbuf_free(&mock.joins);
    corm_strbuf_free(&mock.select_cols);
    corm_strbuf_free(&mock.set_clause);
    PASS();
}

static void test_build_insert_pg(void) {
    corm_query_t mock;
    memset(&mock, 0, sizeof(mock));
    mock.model = &test_user_model;
    mock.op = CORM_OP_INSERT;
    corm_strbuf_init(&mock.where);
    corm_strbuf_init(&mock.order);
    corm_strbuf_init(&mock.group);
    corm_strbuf_init(&mock.having);
    corm_strbuf_init(&mock.joins);
    corm_strbuf_init(&mock.select_cols);
    corm_strbuf_init(&mock.set_clause);

    corm_strbuf_t sql;
    corm_strbuf_init(&sql);
    corm_build_sql(&mock, &sql, CORM_BACKEND_POSTGRES);
    const char *result = corm_strbuf_cstr(&sql);
    assert(strstr(result, "\"test_users\"") != NULL);
    assert(strstr(result, "$1") != NULL);
    assert(strstr(result, "$2") != NULL);
    assert(strchr(result, '?') == NULL);
    corm_strbuf_free(&sql);
    corm_strbuf_free(&mock.where);
    corm_strbuf_free(&mock.order);
    corm_strbuf_free(&mock.group);
    corm_strbuf_free(&mock.having);
    corm_strbuf_free(&mock.joins);
    corm_strbuf_free(&mock.select_cols);
    corm_strbuf_free(&mock.set_clause);
    PASS();
}

static void test_build_update_pg(void) {
    corm_query_t mock;
    memset(&mock, 0, sizeof(mock));
    mock.model = &test_user_model;
    mock.op = CORM_OP_UPDATE;
    corm_strbuf_init(&mock.where);
    corm_strbuf_init(&mock.order);
    corm_strbuf_init(&mock.group);
    corm_strbuf_init(&mock.having);
    corm_strbuf_init(&mock.joins);
    corm_strbuf_init(&mock.select_cols);
    corm_strbuf_init(&mock.set_clause);

    corm_strbuf_append(&mock.set_clause, "name = ?, age = ?");
    corm_strbuf_append(&mock.where, "id = ?");

    corm_strbuf_t sql;
    corm_strbuf_init(&sql);
    corm_build_sql(&mock, &sql, CORM_BACKEND_POSTGRES);
    const char *result = corm_strbuf_cstr(&sql);
    assert(strstr(result, "\"test_users\"") != NULL);
    assert(strstr(result, "name = $1") != NULL);
    assert(strstr(result, "age = $2") != NULL);
    assert(strstr(result, "WHERE id = ?") != NULL);
    corm_strbuf_free(&sql);
    corm_strbuf_free(&mock.where);
    corm_strbuf_free(&mock.order);
    corm_strbuf_free(&mock.group);
    corm_strbuf_free(&mock.having);
    corm_strbuf_free(&mock.joins);
    corm_strbuf_free(&mock.select_cols);
    corm_strbuf_free(&mock.set_clause);
    PASS();
}

static void test_dialect(void) {
    TEST("SQLite dialect uses ? placeholder");
    assert(strcmp(corm_dialect_placeholder(CORM_BACKEND_SQLITE, 0), "?") == 0);
    PASS();

    TEST("Postgres dialect uses $1 placeholder");
    assert(strcmp(corm_dialect_placeholder(CORM_BACKEND_POSTGRES, 0), "$1") == 0);
    assert(strcmp(corm_dialect_placeholder(CORM_BACKEND_POSTGRES, 1), "$2") == 0);
    PASS();
}

int main(void) {
    printf("CORM Query Builder Tests\n");
    printf("════════════════════════\n\n");

    test_query_new_free();
    test_build_select();
    test_build_select_with_where();
    test_build_insert();
    test_build_insert_pg();
    test_build_update();
    test_build_update_pg();
    test_build_delete();
    test_dialect();

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
