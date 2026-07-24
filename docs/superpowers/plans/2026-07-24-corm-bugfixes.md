# CORM Bugfix Trio Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix 3 confirmed bugs in CORM: savepoint SQL injection, PG WHERE placeholder conversion, and unquoted ALTER TABLE identifiers.

**Architecture:** Three independent source-level fixes (src/corm.c, src/builder.c, src/migration.c) with corresponding test updates. Each produces zero new warnings with `-Wall -Wextra -Wunused -Werror`.

**Tech Stack:** C (C99), CMake, SQLite3 (test backend), existing test patterns (TEST/PASS/FAIL macros)

---
## File Structure

| File | Change |
|------|--------|
| `src/corm.c` | Fix #1: add savepoint name validation/quoting in `corm_savepoint`, `corm_rollback_to`, `corm_release_savepoint` |
| `src/builder.c` | Fix #2: extract `append_with_placeholders()` helper, use in all WHERE clause appends for dialect-aware `?` → `$N` conversion |
| `src/migration.c` | Fix #3: quote `table_name` and `f->name` in ALTER TABLE ADD COLUMN |
| `tests/test_query.c` | Fix #2: update `test_build_update_pg` assertion to expect `$3` not `?` in WHERE |
| `tests/test_corm_api.c` | Fix #1: add savepoint injection tests (new file) |


### Task 1: Fix Savepoint SQL Injection

**Files:**
- Modify: `src/corm.c:136-158`
- Create: `tests/test_corm_api.c`
- Modify: `CMakeLists.txt` (add test target)

**Diagnosis:** `corm_savepoint()`, `corm_rollback_to()`, and `corm_release_savepoint()` pass user-supplied `name` directly into `snprintf` without any sanitization. SQL identifiers in SAVEPOINT statements must not contain semicolons, dashes starting the identifier, or other SQL metacharacters.

**Fix:** Validate that `name` only contains alphanumeric characters, underscores, and hyphens (standard SQL identifier charset). Return `CORM_ERR_NULL` (or a new code) for invalid names to fail closed.

- [ ] **Step 1: Write the failing test**

Create `tests/test_corm_api.c`:
```c
#include <corm/corm.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static void test_savepoint_validation(void) {
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    TEST("savepoint rejects SQL injection name");
    printf("SKIP (no sqlite3 backend)\n");
    PASS();
    return;
  }

  /* Valid names should work */
  TEST("savepoint accepts valid name 'sp1'");
  err = corm_savepoint(db, "sp1");
  assert(err == CORM_OK);
  corm_release_savepoint(db, "sp1");
  PASS();

  TEST("savepoint rejects name with semicolon");
  err = corm_savepoint(db, "foo; DROP TABLE users");
  assert(err != CORM_OK);
  PASS();

  TEST("savepoint rejects name with spaces");
  err = corm_savepoint(db, "bad name");
  assert(err != CORM_OK);
  PASS();

  TEST("savepoint rejects empty name");
  err = corm_savepoint(db, "");
  assert(err != CORM_OK);
  PASS();

  TEST("rollback_to rejects SQL injection name");
  err = corm_rollback_to(db, "foo'; DROP TABLE users; --");
  assert(err != CORM_OK);
  PASS();

  TEST("release_savepoint rejects SQL injection name");
  err = corm_release_savepoint(db, "foo'; DROP TABLE users; --");
  assert(err != CORM_OK);
  PASS();

  corm_close(db);
}

int main(void) {
  printf("CORM API Tests\n");
  printf("══════════════\n\n");

  test_savepoint_validation();

  printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
  return tests_failed > 0 ? 1 : 0;
}
```

- [ ] **Step 2: Add test target to CMakeLists.txt**

Add after the existing test_logger block in the `if(CORM_BUILD_TESTS)` section:
```cmake
        add_executable(test_corm_api tests/test_corm_api.c)
        target_link_libraries(test_corm_api corm ${CORM_EXTRA_LIBS})
        add_test(NAME test_corm_api COMMAND test_corm_api)
```

- [ ] **Step 3: Build and verify test fails (injection name accepted)**

```bash
cmake -B build && cmake --build build -j$(nproc) && ./build/test_corm_api
```
Expected: test fails because `; DROP TABLE users` is currently accepted.

- [ ] **Step 4: Implement savepoint name validation**

In `src/corm.c`, replace the three savepoint functions (`corm_savepoint`, `corm_rollback_to`, `corm_release_savepoint`):

```c
static int is_valid_savepoint_name(const char *name) {
  if (!name || !*name)
    return 0;
  for (const char *p = name; *p; p++) {
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '_' || *p == '-'))
      return 0;
  }
  return 1;
}

corm_err_t corm_savepoint(corm_t *db, const char *name) {
  if (!db || !name)
    return CORM_ERR_NULL;
  if (!is_valid_savepoint_name(name)) {
    snprintf(db->err_msg, sizeof(db->err_msg),
             "Invalid savepoint name: '%s'", name);
    return CORM_ERR_NULL;
  }
  char sql[256];
  snprintf(sql, sizeof(sql), "SAVEPOINT %s", name);
  return corm_exec(db, sql);
}
```

Apply the same pattern to `corm_rollback_to` and `corm_release_savepoint`.

- [ ] **Step 5: Build and verify tests pass**

```bash
cmake -B build && cmake --build build -j$(nproc) && ./build/test_corm_api
```
Expected: all savepoint validation tests PASS.

- [ ] **Step 6: Run full test suite**

```bash
cmake -B build && cmake --build build -j$(nproc) && ctest --output-on-failure
```
Expected: all 15 tests pass (14 existing + test_corm_api).

- [ ] **Step 7: Commit**

```bash
git add src/corm.c tests/test_corm_api.c CMakeLists.txt
git commit -m "fix(corm): validate savepoint names to prevent SQL injection"
```


### Task 2: Fix PG WHERE Placeholder Conversion

**Files:**
- Modify: `src/builder.c` (extract helper, apply to SELECT/UPDATE/DELETE WHERE)
- Modify: `tests/test_query.c` (update `test_build_update_pg` assertion)

**Diagnosis:** In `src/builder.c`, the SET clause code (lines 77-92) already converts `?` → dialect-aware `$N`. But the WHERE clause appends (SELECT line 28, UPDATE line 111, DELETE line 118) pass the verbatim `?` placeholder string with no conversion. For SQLite/MySQL this works (they use `?` natively), but for PostgreSQL the WHERE binds remain as literal `?` and fail.

**Fix:** Extract the `?`-replacement loop into a static helper `append_with_placeholders()`, then use it for all three WHERE appends. The param index offset must account for params already consumed by INSERT (autoinc-excluded count) or UPDATE SET.

- [ ] **Step 1: Update the existing PG test to expect correct behavior**

In `tests/test_query.c:test_build_update_pg`, change line 285:
```c
  // BEFORE: assert(strstr(result, "WHERE id = ?") != NULL);
  // AFTER:
  assert(strstr(result, "WHERE id = $3") != NULL);
```

- [ ] **Step 2: Build and verify test fails (WHERE still uses `?`)**

```bash
cmake -B build && cmake --build build -j$(nproc) && ./build/test_query
```
Expected: assertion failure on `WHERE id = $3`.

- [ ] **Step 3: Implement `append_with_placeholders` in builder.c**

Add before `corm_build_sql`:

```c
/* Append `text` rewriting `?` to dialect-aware placeholders starting at
 * `start_index`. Returns the number of placeholders consumed. */
static int append_with_placeholders(corm_strbuf_t *sql, corm_backend_type_t bt,
                                    const char *text, int start_index) {
  int pi = start_index;
  char ph_buf[16];
  while (*text) {
    const char *qm = strchr(text, '?');
    if (qm) {
      corm_strbuf_appendn(sql, text, (size_t)(qm - text));
      corm_dialect_placeholder_str(bt, pi++, ph_buf, sizeof(ph_buf));
      corm_strbuf_append(sql, ph_buf);
      text = qm + 1;
    } else {
      corm_strbuf_append(sql, text);
      break;
    }
  }
  return pi - start_index;
}
```

- [ ] **Step 4: Update SELECT WHERE clause**

Line 27-28: replace `corm_strbuf_appendf(sql, " WHERE %s", ...)` with:
```c
    if (q->where.len > 0) {
      corm_strbuf_append(sql, " WHERE ");
      append_with_placeholders(sql, bt, corm_strbuf_cstr(&q->where), 0);
    }
```

- [ ] **Step 5: Update UPDATE WHERE clause**

Line 110-111: replace with:
```c
    if (q->where.len > 0) {
      int set_param_count;
      if (q->set_clause.len > 0) {
        /* Count ? in set_clause to compute WHERE param offset */
        const char *s = corm_strbuf_cstr(&q->set_clause);
        int pc = 0;
        while (*s) { if (*(s++) == '?') pc++; }
        set_param_count = pc;
      } else {
        /* Non-PK fields are auto-bound in SET */
        int non_pk = 0;
        for (int i = 0; i < q->model->field_count; i++)
          if (!(q->model->fields[i].flags & CORM_FLAG_PRIMARY))
            non_pk++;
        set_param_count = non_pk;
      }
      corm_strbuf_append(sql, " WHERE ");
      append_with_placeholders(sql, bt, corm_strbuf_cstr(&q->where),
                                set_param_count);
    }
```

- [ ] **Step 6: Update DELETE WHERE clause**

Line 117-118: replace with:
```c
    if (q->where.len > 0) {
      corm_strbuf_append(sql, " WHERE ");
      append_with_placeholders(sql, bt, corm_strbuf_cstr(&q->where), 0);
    }
```

- [ ] **Step 7: Update implicit SET path to reuse the same helper**

The existing SET clause code (lines 77-92) already works but uses inline logic. To consolidate, optionally refactor it to use `append_with_placeholders` too. Replace lines 77-92 with:
```c
    if (q->set_clause.len > 0) {
      append_with_placeholders(sql, bt, corm_strbuf_cstr(&q->set_clause), 0);
    }
```

(No need for `pi` counter here — `append_with_placeholders` handles it.)

- [ ] **Step 8: Build and verify tests pass**

```bash
cmake -B build && cmake --build build -j$(nproc) && ctest --output-on-failure
```
Expected: all 15 tests pass.

- [ ] **Step 9: Commit**

```bash
git add src/builder.c tests/test_query.c
git commit -m "fix(builder): convert WHERE ? placeholders to dialect format for PG"
```


### Task 3: Fix ALTER TABLE Identifier Quoting

**Files:**
- Modify: `src/migration.c:84`

**Diagnosis:** In `src/migration.c`, `corm_auto_migrate()` builds ALTER TABLE SQL using raw `%s` for both `m->table_name` and `f->name`. The `create_table()` function correctly quotes identifiers, but ALTER TABLE does not. Any table/column name that is a reserved SQL word (e.g., `group`, `order`, `user`) will break.

**Fix:** Use `corm_dialect_quote()` to wrap both table and column names in the ALTER TABLE SQL.

- [ ] **Step 1: Write a failing test**

Add to `tests/test_migration.c`:
```c
static void test_alter_table_quoting(void) {
  /* We can't easily test ALTER TABLE without a real table.
   * Instead we test that the generated SQL syntax is valid,
   * which we verify by running the full auto_migrate flow. */
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    TEST("ALTER TABLE quoting");
    printf("SKIP (no sqlite3 backend)\n");
    PASS();
    return;
  }

  /* SQLite double-quote identifiers — verify quoted SQL is valid */
  corm_field_t fields[] = {
    {.name = "id", .type = CORM_INT, .offset = 0, .size = sizeof(int),
     .flags = CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC},
    {.name = "name", .type = CORM_STRING, .offset = 4, .size = 64},
    {.name = "group", .type = CORM_STRING, .offset = 68, .size = 64},
  };
  corm_model_t model = {
    .table_name = "test_alter", .struct_size = 132,
    .fields = fields, .field_count = 3, .primary_key = &fields[0],
  };
  corm_model_t *models[] = {&model};

  /* auto_migrate creates table + runs ALTER ADD COLUMN for non-PK fields.
   * If quoting is broken for reserved word 'group', it will fail. */
  corm_exec(db, "CREATE TABLE IF NOT EXISTS test_alter ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT DEFAULT '')");
  /* Now auto_migrate should ADD COLUMN "group" TEXT */
  err = corm_auto_migrate(db, models, 1);
  assert(err == CORM_OK);
  PASS();

  corm_close(db);
}
```

- [ ] **Step 2: Build and verify the reservation is correct**

```bash
cmake -B build && cmake --build build -j$(nproc) && ./build/test_migration
```
The test may or may not catch this depending on how SQLite handles `group` as identifier. The step is informational — the existing `create_table` quoting is the model to follow.

- [ ] **Step 3: Fix ALTER TABLE quoting in migration.c**

Replace line 84:
```c
  // BEFORE:
  snprintf(alter_sql, sizeof(alter_sql), "ALTER TABLE %s ADD COLUMN %s %s;",
           m->table_name, f->name, type_buf);
  // AFTER:
  const char *tq = corm_dialect_quote(backend, m->table_name);
  const char *cq = corm_dialect_quote(backend, f->name);
  snprintf(alter_sql, sizeof(alter_sql), "ALTER TABLE %s%s%s ADD COLUMN %s%s%s %s;",
           tq, m->table_name, tq, cq, f->name, cq, type_buf);
```

- [ ] **Step 4: Build and verify tests pass**

```bash
cmake -B build && cmake --build build -j$(nproc) && ctest --output-on-failure
```
Expected: all 15 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/migration.c tests/test_migration.c
git commit -m "fix(migration): quote identifiers in ALTER TABLE ADD COLUMN"
```


## Verification

| Gate | Command | Expected |
|------|---------|----------|
| All warnings | `cmake -B build -DCMAKE_C_FLAGS="-Wall -Wextra -Wunused -Werror" && cmake --build build -j$(nproc)` | 0 warnings |
| All tests | `ctest --output-on-failure` | 15/15 pass |
| LSP clean | `lsp_diagnostics` on changed files | 0 errors |

## Execution Order

The 3 tasks are independent (different files, no dependencies). They can be implemented in any order.
