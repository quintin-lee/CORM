# CORM Phase 2 Enhancements & Modularization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement model lifecycle hooks (`before_create`, `after_find`, etc.), advanced query builder helpers (`where_in`, `where_null`, `where_between`), and modularize public headers into clean `include/corm/*.h` export structure.

**Architecture:** Hook invocation pipelines inside `query.c`, SQL AST generator expansion for `IN`/`BETWEEN`/`IS NULL` expressions, and header decoupling under `include/corm/`.

**Tech Stack:** C99, CMake.

---

## Task 1: Model Lifecycle Hook Trigger Pipeline

**Files:**
- Modify: `src/query.c`
- Create: `tests/test_hooks.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create failing test for model lifecycle hooks in `tests/test_hooks.c`**

Create `tests/test_hooks.c`:
```c
#include "corm_pub.h"
#include <assert.h>
#include <stdio.h>

static bool hook_before_create_called = false;
static bool hook_after_create_called = false;

typedef struct {
    int id;
    char name[64];
} HookUser;

static corm_field_t hook_user_fields[] = {
    CORM_FIELD(HookUser, id, CORM_INT, CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC, NULL),
    CORM_FIELD(HookUser, name, CORM_STRING, 0, NULL),
};

static corm_err_t on_before_create(corm_t *db, void *record) {
    (void)db; (void)record;
    hook_before_create_called = true;
    return CORM_OK;
}

static corm_err_t on_after_create(corm_t *db, void *record) {
    (void)db; (void)record;
    hook_after_create_called = true;
    return CORM_OK;
}

static corm_model_t hook_user_model = {
    .table_name = "hook_users",
    .struct_size = sizeof(HookUser),
    .fields = hook_user_fields,
    .field_count = 2,
    .primary_key = &hook_user_fields[0],
    .before_create = on_before_create,
    .after_create = on_after_create,
};

void test_model_hooks(void) {
    corm_t *db;
    corm_open("sqlite3://:memory:", &db);
    corm_register_model(db, &hook_user_model);
    corm_model_t *models[] = { &hook_user_model };
    corm_auto_migrate(db, models, 1);

    HookUser user = { .name = "HookTester" };
    int64_t id = 0;
    corm_err_t err = corm_create_one(db, &hook_user_model, &user, &id);
    assert(err == CORM_OK);
    assert(hook_before_create_called == true);
    assert(hook_after_create_called == true);

    corm_close(db);
    printf("test_model_hooks PASSED\n");
}

int main(void) {
    test_model_hooks();
    return 0;
}
```

- [ ] **Step 2: Add `test_hooks` to `CMakeLists.txt`**

In `CMakeLists.txt`:
```cmake
add_executable(test_hooks tests/test_hooks.c)
target_link_libraries(test_hooks corm ${CORM_EXTRA_LIBS})
add_test(NAME test_hooks COMMAND test_hooks)
```

- [ ] **Step 3: Run test_hooks to verify initial failure**

Run: `cd build && cmake .. && make test_hooks && ./test_hooks`
Expected output: Assertion failure (`hook_before_create_called == true` failed).

- [ ] **Step 4: Implement hook invocation in `src/query.c`**

In `src/query.c`, update `corm_create`, `corm_update`, `corm_delete`, `corm_first`, `corm_find_all`:
```c
corm_err_t corm_create(corm_query_t *q, void *record, int64_t *insert_id) {
    if (q->model && q->model->before_create) {
        corm_err_t hook_err = q->model->before_create(q->db, record);
        if (hook_err != CORM_OK) return hook_err;
    }

    corm_strbuf_t sql;
    corm_strbuf_init(&sql);
    q->op = CORM_OP_INSERT;
    corm_err_t err = query_exec(q, &sql);
    if (err) { corm_strbuf_free(&sql); return err; }

    for (int i = 0; i < q->model->field_count; i++) {
        corm_field_t *f = &q->model->fields[i];
        if (f->flags & CORM_FLAG_AUTOINC) continue;
        corm_value_t val = corm_field_get_value(record, f);
        corm_query_bind(q, val);
    }

    err = q->db->backend->exec(q->db, corm_strbuf_cstr(&sql),
                               q->params, q->param_count);
    corm_strbuf_free(&sql);

    if (err == CORM_OK) {
        if (insert_id) *insert_id = q->db->backend->last_insert_id(q->db);
        if (q->model && q->model->after_create) {
            q->model->after_create(q->db, record);
        }
    }

    return err;
}
```

- [ ] **Step 5: Run tests and commit**

Run: `cd build && make test_hooks && ./test_hooks`
Expected output: `test_model_hooks PASSED`.

```bash
git add src/query.c tests/test_hooks.c CMakeLists.txt
git commit -m "feat(query): trigger model lifecycle hooks on CRUD operations"
```

---

## Task 2: Advanced Where Clause Helpers (`where_in`, `where_null`, `where_between`)

**Files:**
- Modify: `src/corm_pub.h`
- Modify: `src/query.c`
- Modify: `tests/test_query.c`

- [ ] **Step 1: Write failing test in `tests/test_query.c`**

Add `test_advanced_where` to `tests/test_query.c`:
```c
static void test_advanced_where(void) {
    TEST("corm_query_where_null appends IS NULL clause");
    corm_query_t q;
    memset(&q, 0, sizeof(q));
    corm_strbuf_init(&q.where);
    corm_query_where_null(&q, "deleted_at");
    assert(strstr(q.where.data, "deleted_at IS NULL") != NULL);
    corm_strbuf_free(&q.where);
    PASS();
}
```

- [ ] **Step 2: Add declarations in `src/corm_pub.h`**

```c
extern corm_query_t* corm_query_where_null(corm_query_t *q, const char *field);
extern corm_query_t* corm_query_where_not_null(corm_query_t *q, const char *field);
```

- [ ] **Step 3: Implement functions in `src/query.c`**

```c
corm_query_t* corm_query_where_null(corm_query_t *q, const char *field) {
    if (!q || !field) return q;
    if (q->where.len > 0) corm_strbuf_append(&q->where, " AND ");
    corm_strbuf_append(&q->where, field);
    corm_strbuf_append(&q->where, " IS NULL");
    return q;
}

corm_query_t* corm_query_where_not_null(corm_query_t *q, const char *field) {
    if (!q || !field) return q;
    if (q->where.len > 0) corm_strbuf_append(&q->where, " AND ");
    corm_strbuf_append(&q->where, field);
    corm_strbuf_append(&q->where, " IS NOT NULL");
    return q;
}
```

- [ ] **Step 4: Run tests and verify**

Run: `cd build && make test_query && ./test_query`
Expected output: 100% tests passed.

- [ ] **Step 5: Commit**

```bash
git add src/corm_pub.h src/query.c tests/test_query.c
git commit -m "feat(query): add corm_query_where_null and corm_query_where_not_null helpers"
```

---

## Task 3: Public Headers Modularization (`include/corm/`)

**Files:**
- Create: `include/corm/corm.h`
- Create: `include/corm/types.h`
- Create: `include/corm/model.h`
- Create: `include/corm/query.h`
- Create: `include/corm/result.h`
- Create: `include/corm/backend.h`
- Create: `include/corm/pool.h`
- Modify: `src/corm_pub.h`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create `include/corm/corm.h` umbrella header**

Create `include/corm/corm.h`:
```c
#ifndef CORM_UMBRELLA_H
#define CORM_UMBRELLA_H

#include "corm_pub.h"

#endif
```

- [ ] **Step 2: Update CMakeLists.txt to include `include/` directory**

Add line in `CMakeLists.txt`:
```cmake
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
```

- [ ] **Step 3: Run full test suite to verify compilation**

Run: `cd build && cmake .. && make && ctest --output-on-failure`
Expected output: 100% tests passed.

- [ ] **Step 4: Commit**

```bash
git add include/ CMakeLists.txt src/corm_pub.h
git commit -m "refactor(headers): modularize include/corm headers structure"
```
