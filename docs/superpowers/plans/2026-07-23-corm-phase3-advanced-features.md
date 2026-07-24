# CORM Phase 3 Enterprise Enhancements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement association preload hydration, prepared statement LRU caching, soft delete support (`deleted_at`), a pluggable slow-query logger interceptor, and standard CMake `cormConfig.cmake` package export.

**Architecture:** Extended query hydration pipeline for preloading related model sets, LRU statement handle cache in connection handles, AST modification for `deleted_at IS NULL` injection, high-resolution execution timers for logging, and CMake package config generation.

**Tech Stack:** C99, CMake, POSIX Threads (`pthread`), libsqlite3, libmysqlclient, libpq.

---

## File Map & Responsibilities

- `include/corm/model.h`: Add `CORM_FLAG_SOFT_DELETE` flag.
- `include/corm/config.h` & `corm.h`: Add pluggable logger types and callback setters (`corm_set_logger`, `corm_log_level_t`).
- `include/corm/query.h`: Add `corm_query_unscoped` and `corm_query_with_deleted` function declarations.
- `src/internal/stmt_cache.h` & `src/internal/stmt_cache.c`: LRU prepared statement handle caching logic.
- `src/query.c`: Preload relation hydration pipeline, soft delete query transformation, and logging invocation.
- `src/corm.c`: Connection handle statement cache initialization & logger callback delegation.
- `CMakeLists.txt`: Add pkg-config (`corm.pc.in`) and CMake package configuration exports (`cormConfig.cmake.in`).
- `tests/`: New test suites (`test_preload.c`, `test_stmt_cache.c`, `test_soft_delete.c`, `test_logger.c`).

---

## Tasks

### Task 1: Association Preload Hydration (`corm_query_preload`)

**Files:**
- Modify: `include/corm/query.h`
- Modify: `src/query.c`
- Create: `tests/test_preload.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing test in `tests/test_preload.c`**

Create `tests/test_preload.c`:
```c
#include "corm/corm.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    int id;
    char title[64];
    int user_id;
} Order;

static corm_field_t order_fields[] = {
    CORM_FIELD(Order, id, CORM_INT, CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC, NULL),
    CORM_FIELD(Order, title, CORM_STRING, 0, NULL),
    CORM_FIELD(Order, user_id, CORM_INT, 0, NULL),
};

static corm_model_t order_model = {
    .table_name = "preload_orders",
    .struct_size = sizeof(Order),
    .fields = order_fields,
    .field_count = 3,
    .primary_key = &order_fields[0],
};

typedef struct {
    int id;
    char name[64];
} PreloadUser;

static corm_field_t user_fields[] = {
    CORM_FIELD(PreloadUser, id, CORM_INT, CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC, NULL),
    CORM_FIELD(PreloadUser, name, CORM_STRING, 0, NULL),
};

static corm_model_t user_model = {
    .table_name = "preload_users",
    .struct_size = sizeof(PreloadUser),
    .fields = user_fields,
    .field_count = 2,
    .primary_key = &user_fields[0],
};

void test_relation_preload(void) {
    corm_t *db;
    corm_open("sqlite3://:memory:", &db);
    corm_register_model(db, &user_model);
    corm_register_model(db, &order_model);

    corm_model_t *models[] = { &user_model, &order_model };
    corm_auto_migrate(db, models, 2);

    PreloadUser u = { .name = "Alice" };
    int64_t uid = 0;
    corm_create_one(db, &user_model, &u, &uid);

    Order o1 = { .title = "Book", .user_id = (int)uid };
    Order o2 = { .title = "Pen", .user_id = (int)uid };
    corm_create_one(db, &order_model, &o1, NULL);
    corm_create_one(db, &order_model, &o2, NULL);

    corm_query_t *q = corm_query_new(db, &user_model);
    corm_query_preload(q, "preload_orders");
    corm_result_t *res = NULL;
    corm_err_t err = corm_find(q, &res);
    assert(err == CORM_OK);
    assert(res->row_count == 1);

    corm_result_release(res);
    corm_query_free(q);
    corm_close(db);
    printf("test_relation_preload PASSED\n");
}

int main(void) {
    test_relation_preload();
    return 0;
}
```

- [ ] **Step 2: Add `test_preload` executable to `CMakeLists.txt`**

```cmake
add_executable(test_preload tests/test_preload.c)
target_link_libraries(test_preload corm ${CORM_EXTRA_LIBS})
add_test(NAME test_preload COMMAND test_preload)
```

- [ ] **Step 3: Implement relation tracking in `src/query.c`**

In `src/query.c`, update `corm_query_preload` to register target relation tables to be preloaded.

- [ ] **Step 4: Run test_preload to verify**

Run: `cd build && cmake .. && make test_preload && ./test_preload`
Expected output: `test_relation_preload PASSED`.

- [ ] **Step 5: Commit**

```bash
git add src/query.c include/corm/query.h tests/test_preload.c CMakeLists.txt
git commit -m "feat(query): implement relation preload query tracking"
```

---

### Task 2: Prepared Statement LRU Cache (`corm_stmt_cache_t`)

**Files:**
- Create: `src/internal/stmt_cache.h`
- Create: `src/internal/stmt_cache.c`
- Modify: `include/corm/corm.h`
- Modify: `src/corm.c`
- Create: `tests/test_stmt_cache.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing test in `tests/test_stmt_cache.c`**

Create `tests/test_stmt_cache.c`:
```c
#include "corm/corm.h"
#include "internal/stmt_cache.h"
#include <assert.h>
#include <stdio.h>

void test_statement_cache(void) {
    corm_stmt_cache_t *cache = NULL;
    corm_err_t err = corm_stmt_cache_create(4, &cache);
    assert(err == CORM_OK);

    corm_stmt_cache_put(cache, "SELECT 1", (void*)0x1234);
    void *stmt = corm_stmt_cache_get(cache, "SELECT 1");
    assert(stmt == (void*)0x1234);

    corm_stmt_cache_destroy(cache);
    printf("test_statement_cache PASSED\n");
}

int main(void) {
    test_statement_cache();
    return 0;
}
```

- [ ] **Step 2: Add `test_stmt_cache` to `CMakeLists.txt`**

```cmake
add_executable(test_stmt_cache tests/test_stmt_cache.c src/internal/stmt_cache.c)
target_link_libraries(test_stmt_cache corm ${CORM_EXTRA_LIBS})
add_test(NAME test_stmt_cache COMMAND test_stmt_cache)
```

- [ ] **Step 3: Implement statement cache in `src/internal/stmt_cache.c`**

Create `src/internal/stmt_cache.h` and `src/internal/stmt_cache.c` implementing an LRU cache with hash table lookup.

- [ ] **Step 4: Run test_stmt_cache**

Run: `cd build && cmake .. && make test_stmt_cache && ./test_stmt_cache`
Expected output: `test_statement_cache PASSED`.

- [ ] **Step 5: Commit**

```bash
git add src/internal/stmt_cache.h src/internal/stmt_cache.c tests/test_stmt_cache.c CMakeLists.txt
git commit -m "feat(cache): implement LRU prepared statement handle cache"
```

---

### Task 3: Soft Delete Mechanism (`deleted_at`)

**Files:**
- Modify: `include/corm/model.h`
- Modify: `include/corm/query.h`
- Modify: `src/query.c`
- Create: `tests/test_soft_delete.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write test for Soft Delete in `tests/test_soft_delete.c`**

Create `tests/test_soft_delete.c`:
```c
#include "corm/corm.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    int id;
    char name[64];
    char deleted_at[32];
} SoftUser;

static corm_field_t soft_fields[] = {
    CORM_FIELD(SoftUser, id, CORM_INT, CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC, NULL),
    CORM_FIELD(SoftUser, name, CORM_STRING, 0, NULL),
    CORM_FIELD(SoftUser, deleted_at, CORM_STRING, CORM_FLAG_SOFT_DELETE, NULL),
};

static corm_model_t soft_user_model = {
    .table_name = "soft_users",
    .struct_size = sizeof(SoftUser),
    .fields = soft_fields,
    .field_count = 3,
    .primary_key = &soft_fields[0],
};

void test_soft_delete(void) {
    corm_t *db;
    corm_open("sqlite3://:memory:", &db);
    corm_register_model(db, &soft_user_model);
    corm_model_t *models[] = { &soft_user_model };
    corm_auto_migrate(db, models, 1);

    SoftUser u = { .name = "SoftAlice" };
    int64_t id = 0;
    corm_create_one(db, &soft_user_model, &u, &id);

    // Perform delete -> should turn into update deleted_at
    corm_query_t *q1 = corm_query_new(db, &soft_user_model);
    corm_query_where(q1, "id = 1");
    int affected = 0;
    assert(corm_delete(q1, &affected) == CORM_OK);
    corm_query_free(q1);

    // Query normal find -> should filter out soft-deleted row
    corm_query_t *q2 = corm_query_new(db, &soft_user_model);
    corm_result_t *res = NULL;
    corm_find(q2, &res);
    assert(res->row_count == 0);
    corm_result_release(res);
    corm_query_free(q2);

    // Query unscoped find -> should return row
    corm_query_t *q3 = corm_query_new(db, &soft_user_model);
    corm_query_unscoped(q3);
    corm_find(q3, &res);
    assert(res->row_count == 1);
    corm_result_release(res);
    corm_query_free(q3);

    corm_close(db);
    printf("test_soft_delete PASSED\n");
}

int main(void) {
    test_soft_delete();
    return 0;
}
```

- [ ] **Step 2: Add `CORM_FLAG_SOFT_DELETE` to `include/corm/model.h`**

```c
#define CORM_FLAG_SOFT_DELETE (1<<4)
```

- [ ] **Step 3: Implement soft delete logic and `corm_query_unscoped` in `src/query.c`**

In `corm_delete`: if model has a field with `CORM_FLAG_SOFT_DELETE`, execute `UPDATE table SET deleted_at = ... WHERE ...`.
In `corm_find`: if model has soft delete field and `!q->unscoped`, append `AND deleted_at IS NULL`.

- [ ] **Step 4: Run test_soft_delete**

Run: `cd build && cmake .. && make test_soft_delete && ./test_soft_delete`
Expected output: `test_soft_delete PASSED`.

- [ ] **Step 5: Commit**

```bash
git add include/corm/model.h include/corm/query.h src/query.c tests/test_soft_delete.c CMakeLists.txt
git commit -m "feat(query): implement soft delete mechanism with CORM_FLAG_SOFT_DELETE and unscoped queries"
```

---

### Task 4: Pluggable Logger Interceptor & Slow Query Detector

**Files:**
- Modify: `include/corm/config.h`
- Modify: `include/corm/corm.h`
- Modify: `src/corm.c`
- Modify: `src/backend/sqlite3.c`
- Create: `tests/test_logger.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write test for logger interceptor in `tests/test_logger.c`**

Create `tests/test_logger.c`:
```c
#include "corm/corm.h"
#include <assert.h>
#include <stdio.h>

static bool logger_called = false;

static void my_logger(corm_log_level_t level, const char *sql, uint64_t elapsed_us, void *user_data) {
    (void)level; (void)elapsed_us; (void)user_data;
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
```

- [ ] **Step 2: Add logger types and `corm_set_logger` signature**

In `include/corm/config.h`:
```c
typedef enum {
    CORM_LOG_DEBUG,
    CORM_LOG_INFO,
    CORM_LOG_WARN,
    CORM_LOG_ERROR
} corm_log_level_t;

typedef void (*corm_logger_fn)(corm_log_level_t level, const char *sql, uint64_t elapsed_us, void *user_data);
```

In `include/corm/corm.h`:
```c
extern void corm_set_logger(corm_t *db, corm_logger_fn logger, void *user_data);
```

- [ ] **Step 3: Implement timing and logger callbacks in `src/corm.c` and driver executions**

Record execution start time (`clock_gettime`) and invoke `db->logger` upon SQL execution finish.

- [ ] **Step 4: Run test_logger**

Run: `cd build && cmake .. && make test_logger && ./test_logger`
Expected output: `test_logger_interceptor PASSED`.

- [ ] **Step 5: Commit**

```bash
git add include/corm/config.h include/corm/corm.h src/corm.c src/backend/sqlite3.c tests/test_logger.c CMakeLists.txt
git commit -m "feat(log): implement pluggable logger interceptor and execution timing measurement"
```

---

### Task 5: CMake Package Configuration Export (`cormConfig.cmake`)

**Files:**
- Create: `corm.pc.in`
- Create: `cormConfig.cmake.in`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create `corm.pc.in` template**

Create `corm.pc.in`:
```ini
prefix=@CMAKE_INSTALL_PREFIX@
exec_prefix=${prefix}
libdir=${prefix}/lib
includedir=${prefix}/include

Name: corm
Description: C Database Adapter Layer (GORM-inspired)
Version: @PROJECT_VERSION@
Libs: -L${libdir} -lcorm
Cflags: -I${includedir}
```

- [ ] **Step 2: Create `cormConfig.cmake.in` template**

Create `cormConfig.cmake.in`:
```cmake
@PACKAGE_INIT@

include("${CMAKE_CURRENT_LIST_DIR}/cormTargets.cmake")
check_required_components(corm)
```

- [ ] **Step 3: Update `CMakeLists.txt` to generate and install package configs**

Use `configure_file` and `install(EXPORT cormTargets ...)` in `CMakeLists.txt`.

- [ ] **Step 4: Test install rule**

Run: `cd build && cmake .. && make && ctest --output-on-failure`
Expected output: 100% tests passed.

- [ ] **Step 5: Commit**

```bash
git add corm.pc.in cormConfig.cmake.in CMakeLists.txt
git commit -m "build(cmake): export cormConfig.cmake and pkg-config corm.pc files"
```

---

## Execution Choice Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-23-corm-phase3-advanced-features.md`.

Two execution options:

1. **Subagent-Driven (recommended)** - Dispatch a fresh subagent per task, review between tasks, fast iteration.
2. **Inline Execution** - Execute tasks in this session using `executing-plans`, batch execution with checkpoints.

Which approach would you like to use?
