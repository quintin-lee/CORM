# CORM Systemic Optimization & Feature Expansion Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement full model lifecycle hooks (`update`, `delete`, `find`), advanced query predicates (`where_in`, `where_between`), connection pool timed wait & health checks, modular header file decoupling, and ASan memory sanitizer integration.

**Architecture:** Hook pipeline execution across all CRUD functions in `query.c`, parameter-bound SQL AST extension for `IN` and `BETWEEN` expressions, `pthread_cond_timedwait` for pool acquisition with auto-reconnect ping, and clean standalone headers under `include/corm/`.

**Tech Stack:** C99, CMake, POSIX Threads (`pthread`), libsqlite3, libmysqlclient, libpq.

---

## File Map & Responsibilities

- `include/corm/types.h`: Public types, enums, values, error codes.
- `include/corm/config.h`: Connection configuration options.
- `include/corm/backend.h`: Backend driver vtable definitions.
- `include/corm/model.h`: Model, field, relation, and hook descriptors.
- `include/corm/result.h`: Result set iteration and column value accessors.
- `include/corm/query.h`: Query builder chainable interface and execution.
- `include/corm/pool.h`: Public thread-safe connection pool API.
- `include/corm/corm.h`: Top-level umbrella header.
- `src/query.c`: Full hook pipeline execution (`before_update`, `after_update`, `before_delete`, `after_delete`, `after_find`) & predicate builders (`corm_query_where_in`, `corm_query_where_between`).
- `src/internal/pool.c`: Timed wait on pool acquisition using `pthread_cond_timedwait` and `corm_ping` health check.
- `CMakeLists.txt`: Clean `include/corm/` export target and `CORM_ENABLE_ASAN` Option.
- `tests/`: Automated unit test suites verifying each feature.

---

## Tasks

### Task 1: Complete Lifecycle Hooks Pipeline (`update`, `delete`, `find`)

**Files:**
- Modify: `src/query.c`
- Create: `tests/test_full_hooks.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing test in `tests/test_full_hooks.c` for update, delete, and find hooks**

Create `tests/test_full_hooks.c`:
```c
#include "corm_pub.h"
#include <assert.h>
#include <stdio.h>

static bool before_update_called = false;
static bool after_update_called = false;
static bool before_delete_called = false;
static bool after_delete_called = false;
static bool after_find_called = false;

typedef struct {
    int id;
    char name[64];
} HookRecord;

static corm_field_t rec_fields[] = {
    CORM_FIELD(HookRecord, id, CORM_INT, CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC, NULL),
    CORM_FIELD(HookRecord, name, CORM_STRING, 0, NULL),
};

static corm_err_t on_before_update(corm_t *db, void *record) { (void)db; (void)record; before_update_called = true; return CORM_OK; }
static corm_err_t on_after_update(corm_t *db, void *record) { (void)db; (void)record; after_update_called = true; return CORM_OK; }
static corm_err_t on_before_delete(corm_t *db, void *record) { (void)db; (void)record; before_delete_called = true; return CORM_OK; }
static corm_err_t on_after_delete(corm_t *db, void *record) { (void)db; (void)record; after_delete_called = true; return CORM_OK; }
static corm_err_t on_after_find(corm_t *db, void *record) { (void)db; (void)record; after_find_called = true; return CORM_OK; }

static corm_model_t full_hook_model = {
    .table_name = "full_hook_records",
    .struct_size = sizeof(HookRecord),
    .fields = rec_fields,
    .field_count = 2,
    .primary_key = &rec_fields[0],
    .before_update = on_before_update,
    .after_update = on_after_update,
    .before_delete = on_before_delete,
    .after_delete = on_after_delete,
    .after_find = on_after_find,
};

void test_all_lifecycle_hooks(void) {
    corm_t *db;
    corm_open("sqlite3://:memory:", &db);
    corm_register_model(db, &full_hook_model);
    corm_model_t *models[] = { &full_hook_model };
    corm_auto_migrate(db, models, 1);

    HookRecord rec = { .name = "Initial" };
    int64_t id = 0;
    corm_create_one(db, &full_hook_model, &rec, &id);

    // Test first & after_find hook
    corm_query_t *q1 = corm_query_new(db, &full_hook_model);
    HookRecord found_rec;
    corm_err_t err = corm_first(q1, &found_rec);
    assert(err == CORM_OK);
    assert(after_find_called == true);
    corm_query_free(q1);

    // Test update hooks
    corm_query_t *q2 = corm_query_new(db, &full_hook_model);
    corm_query_where(q2, "id = 1");
    corm_value_t new_name = { .type = CORM_STRING, .v.s = "Updated" };
    corm_query_set(q2, "name", new_name);
    int affected = 0;
    err = corm_update(q2, &affected);
    assert(err == CORM_OK);
    assert(before_update_called == true);
    assert(after_update_called == true);
    corm_query_free(q2);

    // Test delete hooks
    corm_query_t *q3 = corm_query_new(db, &full_hook_model);
    corm_query_where(q3, "id = 1");
    err = corm_delete(q3, &affected);
    assert(err == CORM_OK);
    assert(before_delete_called == true);
    assert(after_delete_called == true);
    corm_query_free(q3);

    corm_close(db);
    printf("test_all_lifecycle_hooks PASSED\n");
}

int main(void) {
    test_all_lifecycle_hooks();
    return 0;
}
```

- [ ] **Step 2: Add `test_full_hooks` executable to `CMakeLists.txt`**

```cmake
add_executable(test_full_hooks tests/test_full_hooks.c)
target_link_libraries(test_full_hooks corm ${CORM_EXTRA_LIBS})
add_test(NAME test_full_hooks COMMAND test_full_hooks)
```

- [ ] **Step 3: Run `make test_full_hooks` to confirm initial failure**

Run: `cd build && cmake .. && make test_full_hooks && ./test_full_hooks`
Expected output: Assertion failure on `after_find_called == true` or `before_update_called == true`.

- [ ] **Step 4: Update `src/query.c` to execute hooks across `corm_first`, `corm_find_all`, `corm_update`, and `corm_delete`**

In `src/query.c`:
- In `corm_first`: trigger `q->model->after_find` after record hydration.
- In `corm_find_all`: trigger `model->after_find` for each hydrated record.
- In `corm_update`: trigger `q->model->before_update` before exec, and `q->model->after_update` after exec.
- In `corm_delete`: trigger `q->model->before_delete` before exec, and `q->model->after_delete` after exec.

- [ ] **Step 5: Run tests and commit**

Run: `cd build && make test_full_hooks && ./test_full_hooks`
Expected output: `test_all_lifecycle_hooks PASSED`.

```bash
git add src/query.c tests/test_full_hooks.c CMakeLists.txt
git commit -m "feat(query): complete full lifecycle hook execution pipeline for find, update, and delete"
```

---

### Task 2: Advanced Predicates (`where_in`, `where_between`)

**Files:**
- Modify: `src/corm_pub.h`
- Modify: `src/query.c`
- Create: `tests/test_predicates.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create failing test `tests/test_predicates.c`**

Create `tests/test_predicates.c`:
```c
#include "corm_pub.h"
#include <assert.h>
#include <stdio.h>

void test_where_in_and_between(void) {
    corm_query_t q;
    memset(&q, 0, sizeof(q));
    corm_strbuf_init(&q.where);

    corm_value_t vals[3];
    vals[0].type = CORM_INT; vals[0].v.i = 10;
    vals[1].type = CORM_INT; vals[1].v.i = 20;
    vals[2].type = CORM_INT; vals[2].v.i = 30;

    corm_query_where_in(&q, "age", vals, 3);
    assert(strstr(corm_strbuf_cstr(&q.where), "age IN (?, ?, ?)") != NULL);
    assert(q.param_count == 3);

    corm_value_t min_val = { .type = CORM_INT, .v.i = 1 };
    corm_value_t max_val = { .type = CORM_INT, .v.i = 100 };
    corm_query_where_between(&q, "score", min_val, max_val);
    assert(strstr(corm_strbuf_cstr(&q.where), "score BETWEEN ? AND ?") != NULL);
    assert(q.param_count == 5);

    corm_strbuf_free(&q.where);
    if (q.params) free(q.params);
    printf("test_where_in_and_between PASSED\n");
}

int main(void) {
    test_where_in_and_between();
    return 0;
}
```

- [ ] **Step 2: Declare `corm_query_where_in` and `corm_query_where_between` in `src/corm_pub.h`**

```c
extern corm_query_t* corm_query_where_in(corm_query_t *q, const char *field, corm_value_t *vals, int count);
extern corm_query_t* corm_query_where_between(corm_query_t *q, const char *field, corm_value_t min_val, corm_value_t max_val);
```

- [ ] **Step 3: Implement functions in `src/query.c`**

Implement `corm_query_where_in` and `corm_query_where_between` with placeholder generation and parameter binding.

- [ ] **Step 4: Build and run test_predicates**

Run: `cd build && cmake .. && make test_predicates && ./test_predicates`
Expected output: `test_where_in_and_between PASSED`.

- [ ] **Step 5: Commit**

```bash
git add src/corm_pub.h src/query.c tests/test_predicates.c CMakeLists.txt
git commit -m "feat(query): add corm_query_where_in and corm_query_where_between predicate builders"
```

---

### Task 3: Connection Pool Timed Wait Timeout & Health Check

**Files:**
- Modify: `src/internal/pool.c`
- Modify: `tests/test_pool.c`

- [ ] **Step 1: Write test for timed wait and health check in `tests/test_pool.c`**

Add `test_pool_timeout_and_ping` to `tests/test_pool.c`:
```c
void test_pool_timeout(void) {
    corm_config_t cfg = { .max_open_conns = 1, .max_idle_conns = 1, .timeout_ms = 100 };
    corm_pool_t *pool = NULL;
    assert(corm_pool_create("sqlite3://:memory:", cfg, &pool) == CORM_OK);

    corm_t *db1 = NULL, *db2 = NULL;
    assert(corm_pool_acquire(pool, &db1) == CORM_OK);

    // Attempt acquire when max_open reached and timed out
    corm_err_t err = corm_pool_acquire(pool, &db2);
    assert(err == CORM_ERR_BACKEND || err == CORM_ERR_GENERIC || err == CORM_ERR_NOMEM || err != CORM_OK);

    assert(corm_pool_release(pool, db1) == CORM_OK);
    corm_pool_destroy(pool);
    printf("test_pool_timeout PASSED\n");
}
```

- [ ] **Step 2: Implement `pthread_cond_timedwait` & `corm_ping` check in `src/internal/pool.c`**

In `corm_pool_acquire`:
```c
if (pool->config.timeout_ms > 0) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += pool->config.timeout_ms / 1000;
    ts.tv_nsec += (pool->config.timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }
    int rc = pthread_cond_timedwait(&pool->cond, &pool->lock, &ts);
    if (rc != 0 && !pool->idle_head) {
        pthread_mutex_unlock(&pool->lock);
        return CORM_ERR_GENERIC;
    }
}
```
Also verify `corm_ping(db)` on idle connections before returning.

- [ ] **Step 3: Run test_pool to verify**

Run: `cd build && make test_pool && ./test_pool`
Expected output: `test_connection_pool PASSED` and `test_pool_timeout PASSED`.

- [ ] **Step 4: Commit**

```bash
git add src/internal/pool.c tests/test_pool.c
git commit -m "feat(pool): implement timed wait timeout and corm_ping connection health check"
```

---

### Task 4: Standalone Public Modular Headers (`include/corm/*.h`)

**Files:**
- Create: `include/corm/types.h`
- Create: `include/corm/config.h`
- Create: `include/corm/backend.h`
- Create: `include/corm/model.h`
- Create: `include/corm/result.h`
- Create: `include/corm/query.h`
- Create: `include/corm/pool.h`
- Modify: `include/corm/corm.h`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create standalone header files under `include/corm/`**

Extract clean public API definitions from `src/corm_pub.h` into independent headers:
- `include/corm/types.h`: `corm_err_t`, `corm_value_t`, `corm_field_type_t`, error codes.
- `include/corm/config.h`: `corm_config_t`, `CORM_DEFAULT_CONFIG`.
- `include/corm/backend.h`: `corm_backend_t` vtable struct & functions.
- `include/corm/model.h`: `corm_field_t`, `corm_model_t`, `corm_relation_t`, `CORM_FIELD` macro.
- `include/corm/result.h`: `corm_result_t` and accessors.
- `include/corm/query.h`: `corm_query_t` and query builder API.
- `include/corm/pool.h`: `corm_pool_t` and connection pool API.
- `include/corm/corm.h`: Umbrella header including all above.

- [ ] **Step 2: Update CMakeLists.txt to install headers**

Add header install target in `CMakeLists.txt`:
```cmake
install(DIRECTORY include/corm DESTINATION include)
```

- [ ] **Step 3: Build and run test suite**

Run: `cd build && cmake .. && make && ctest --output-on-failure`
Expected output: 100% tests passed.

- [ ] **Step 4: Commit**

```bash
git add include/corm/ CMakeLists.txt
git commit -m "refactor(headers): split public headers into modular standalone files under include/corm/"
```

---

### Task 5: ASan Memory Sanitizer CMake Integration & Full Verification

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add `CORM_ENABLE_ASAN` option to `CMakeLists.txt`**

In `CMakeLists.txt`:
```cmake
option(CORM_ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(CORM_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
    message(STATUS "AddressSanitizer: ENABLED")
endif()
```

- [ ] **Step 2: Build with ASan enabled and run all tests**

Run: `cd build && cmake .. -DCORM_ENABLE_ASAN=ON && make && ctest --output-on-failure`
Expected output: 100% tests passed without any ASan memory errors.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build(cmake): add CORM_ENABLE_ASAN option for AddressSanitizer automated leak auditing"
```

---

## Execution Choice Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-23-corm-systemic-optimization.md`.

Two execution options:

1. **Subagent-Driven (recommended)** - Dispatch a fresh subagent per task, review between tasks, fast iteration.
2. **Inline Execution** - Execute tasks in this session using `executing-plans`, batch execution with checkpoints.

Which approach would you like to use?
