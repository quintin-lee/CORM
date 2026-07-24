# Migration Enhancement: Schema Diff + Index + FK Implementation Plan

> **For agentic workers:** Execute sequentially — Task 1 is a prerequisite for Tasks 3-4 (field_t struct changes affect CORM_FIELD macro and index/FK DDL). Tasks 2/4 can run after Task 1. Task 5 is independent.

**Goal:** Upgrade `corm_auto_migrate()` to introspect existing table schemas via a backend `describe_table` hook, skip already-existing columns, create indexes for `CORM_FLAG_INDEX`-flagged fields, and generate `REFERENCES` clause for foreign keys.

**Architecture:** New backend vtable hook for schema introspection; smart migration DDL builder that reads current schema, diffs against model, creates only missing columns + indexes + FK constraints.

**Tech Stack:** C99, CMake, SQLite3 (test backend via `PRAGMA table_info` / `PRAGMA index_list`)

---
## File Structure

| File | Change |
|------|--------|
| `include/corm/model.h` | Add `CORM_FLAG_INDEX` flag, add `fk_ref` field to `corm_field_t`, update `CORM_FIELD` macro |
| `include/corm/backend.h` | Add `describe_table` vtable entry |
| `src/backend/sqlite3.c` | Implement `sqlite_describe_table` via `PRAGMA table_info + index_list` |
| `src/backend/mysql.c` | Stub `mysql_describe_table` returning `CORM_ERR_UNSUPPORTED` |
| `src/backend/postgres.c` | Stub `pg_describe_table` returning `CORM_ERR_UNSUPPORTED` |
| `src/migration.c` | Rewrite `corm_auto_migrate()`: introspect, diff, create indexes, FK REFERENCES |
| `src/dialect.c` / `src/dialect.h` | Add `corm_dialect_fk_on_delete()` for `ON DELETE CASCADE / SET NULL` |
| `tests/test_migration.c` | Add index creation test, FK test, no-op re-migration test |
| `include/corm/corm.h` | No changes needed (auto_migrate signature unchanged) |

### Task 1: Extend corm_field_t with INDEX flag and FK metadata

**Files:**
- Modify: `include/corm/model.h`

**Design:** Add `CORM_FLAG_INDEX` bit to `corm_field_flags_t`, add `fk_ref` string field to `corm_field_t`, update `CORM_FIELD` macro with default `NULL` for `fk_ref`.

- [ ] **Step 1: Add `CORM_FLAG_INDEX` to the flags enum**

```c
typedef enum {
  CORM_FLAG_PRIMARY    = 1 << 0,
  CORM_FLAG_AUTOINC    = 1 << 1,
  CORM_FLAG_NOT_NULL   = 1 << 2,
  CORM_FLAG_UNIQUE     = 1 << 3,
  CORM_FLAG_SOFT_DELETE = 1 << 4,
  CORM_FLAG_INDEX      = 1 << 5,  /* NEW */
} corm_field_flags_t;
```

- [ ] **Step 2: Add `fk_ref` field to `corm_field_t`**

```c
typedef struct {
  const char *name;
  corm_field_type_t type;
  size_t offset;
  size_t size;
  int flags;
  const char *default_value;
  const char *fk_ref;    /* NEW: "other_table.other_column" or NULL */
} corm_field_t;
```

- [ ] **Step 3: Update `CORM_FIELD` macro**

Add `, .fk_ref = NULL` at the end so existing callers are unaffected:
```c
#define CORM_FIELD(name_, type_, offset_, size_, flags_, default_)\
  {.name = name_, .type = type_, .offset = offset_, .size = size_,\
   .flags = flags_, .default_value = default_, .fk_ref = NULL}
```

- [ ] **Step 4: Build and verify**

```bash
cmake -B build && cmake --build build -j$(nproc) 2>&1 | grep -c "error\|warning"
```
Expected: 0.

- [ ] **Step 5: Commit**

```bash
git add include/corm/model.h
git commit -m "feat(model): add CORM_FLAG_INDEX and fk_ref for FK metadata"
```


### Task 2: Add describe_table backend hook + SQLite implementation

**Files:**
- Modify: `include/corm/backend.h` (add `describe_table` to vtable)
- Modify: `src/backend/sqlite3.c` (implement via `PRAGMA table_info` + `PRAGMA index_list`)
- Modify: `src/backend/mysql.c` (stub `CORM_ERR_UNSUPPORTED`)
- Modify: `src/backend/postgres.c` (stub `CORM_ERR_UNSUPPORTED`)
- Create: `include/corm/backend.h` — confirm `corm_table_info_t` struct

- [ ] **Step 1: Define `corm_column_info_t` and `corm_table_info_t` result types**

Add to `include/corm/backend.h`:
```c
/** Column metadata returned by describe_table */
typedef struct {
  char *name;
  corm_field_type_t type;
  int not_null;
  int is_primary;
  int has_default;
  char *default_value;
} corm_column_info_t;

/** Table schema returned by describe_table */
typedef struct {
  corm_column_info_t *columns;
  int column_count;
  char **index_names;   /* index names from PRAGMA index_list */
  int index_count;
} corm_table_info_t;

/** Free a table_info result */
void corm_table_info_free(corm_table_info_t *info);
```

- [ ] **Step 2: Add `describe_table` to the backend vtable**

```c
typedef struct {
  // ...existing entries...
  corm_err_t (*describe_table)(corm_t *db, const char *table_name,
                                corm_table_info_t *out);
} corm_backend_t;
```

- [ ] **Step 3: Implement `corm_table_info_free`**

In `src/backend/backend.c`:
```c
void corm_table_info_free(corm_table_info_t *info) {
  if (!info) return;
  for (int i = 0; i < info->column_count; i++) {
    free(info->columns[i].name);
    free(info->columns[i].default_value);
  }
  free(info->columns);
  for (int i = 0; i < info->index_count; i++)
    free(info->index_names[i]);
  free(info->index_names);
}
```

- [ ] **Step 4: Implement `sqlite_describe_table` in sqlite3.c**

```c
static corm_err_t sqlite_describe_table(corm_t *db, const char *table_name,
                                         corm_table_info_t *out) {
  sqlite3 *handle = (sqlite3 *)db->conn;
  memset(out, 0, sizeof(*out));

  /* PRAGMA table_info returns: cid, name, type, notnull, dflt_value, pk */
  char sql[256];
  snprintf(sql, sizeof(sql), "PRAGMA table_info('%s')", table_name);
  corm_result_t *res = NULL;
  corm_err_t err = corm_raw(db, sql, &res);
  if (err || !res) return err ? err : CORM_ERR_BACKEND;

  out->column_count = res->row_count;
  out->columns = calloc((size_t)out->column_count, sizeof(corm_column_info_t));
  for (int r = 0; r < res->row_count; r++) {
    corm_column_info_t *col = &out->columns[r];
    /* col 0: cid (int) — skip */
    /* col 1: name (text) */
    corm_value_t *name_v = &res->rows[r][1];
    col->name = name_v->v.s ? strdup(name_v->v.s) : NULL;
    /* col 2: type (text) — map to corm_field_type_t */
    corm_value_t *type_v = &res->rows[r][2];
    if (type_v->v.s) {
      if (strstr(type_v->v.s, "INT")) col->type = CORM_INT64;
      else if (strstr(type_v->v.s, "REAL") || strstr(type_v->v.s, "FLOAT") || strstr(type_v->v.s, "DOUBLE")) col->type = CORM_DOUBLE;
      else if (strstr(type_v->v.s, "BLOB")) col->type = CORM_BLOB;
      else col->type = CORM_TEXT;
    }
    /* col 3: notnull (int) */
    col->not_null = (int)res->rows[r][3].v.i;
    /* col 4: dflt_value (text, nullable) */
    corm_value_t *dflt = &res->rows[r][4];
    if (!dflt->is_null && dflt->v.s) col->default_value = strdup(dflt->v.s);
    /* col 5: pk (int) */
    col->is_primary = (int)res->rows[r][5].v.i != 0;
  }
  corm_result_release(res);

  /* PRAGMA index_list(table) returns: seq, name, unique, origin, partial */
  snprintf(sql, sizeof(sql), "PRAGMA index_list('%s')", table_name);
  err = corm_raw(db, sql, &res);
  if (err || !res) return err ? err : CORM_ERR_BACKEND;

  out->index_count = res->row_count;
  out->index_names = calloc((size_t)out->index_count, sizeof(char *));
  for (int r = 0; r < res->row_count; r++) {
    corm_value_t *name_v = &res->rows[r][1];  /* col 1: name */
    if (!name_v->is_null && name_v->v.s)
      out->index_names[r] = strdup(name_v->v.s);
  }
  corm_result_release(res);

  return CORM_OK;
}
```

- [ ] **Step 5: Wire the vtable entry**

In the sqlite3 backend registration, add `.describe_table = sqlite_describe_table`.

For mysql.c: add `.describe_table = NULL` (or a stub returning `CORM_ERR_UNSUPPORTED`).
For postgres.c: same.

- [ ] **Step 6: Build and verify**

```bash
cmake -B build && cmake --build build -j$(nproc) && ctest --output-on-failure
```
Expected: 0 warnings, 15/15 tests pass.

- [ ] **Step 7: Commit**

```bash
git add include/corm/backend.h src/backend/sqlite3.c src/backend/mysql.c src/backend/postgres.c src/backend/backend.c
git commit -m "feat(backend): add describe_table vtable hook with SQLite PRAGMA impl"
```


### Task 3: Smart schema diff in corm_auto_migrate

**Files:**
- Modify: `src/migration.c`

**Design:** Rewrite `corm_auto_migrate()`:
1. Attempt `describe_table` — if unsupported, fall back to old behavior
2. Build a set of existing column names from the returned info
3. For each model field: if column missing, ALTER TABLE ADD COLUMN (existing behavior)
4. For each field with `CORM_FLAG_INDEX`: if not in existing index list, CREATE INDEX

- [ ] **Step 1: Add a `column_exists` helper**

```c
static int column_exists(corm_table_info_t *info, const char *name) {
  for (int i = 0; i < info->column_count; i++)
    if (info->columns[i].name && strcmp(info->columns[i].name, name) == 0)
      return 1;
  return 0;
}
```

- [ ] **Step 2: Add a `index_exists` helper**

```c
static int index_exists(corm_table_info_t *info, const char *table,
                        const char *column) {
  /* SQLite auto-names indexes as: sqlite_autoindex_<table>_N or we name them
   * ourselves as idx_<table>_<column> */
  char expected[256];
  snprintf(expected, sizeof(expected), "idx_%s_%s", table, column);
  for (int i = 0; i < info->index_count; i++)
    if (info->index_names[i] && strcmp(info->index_names[i], expected) == 0)
      return 1;
  return 0;
}
```

- [ ] **Step 3: Rewrite `corm_auto_migrate`**

```c
corm_err_t corm_auto_migrate(corm_t *db, corm_model_t *models[], int count) {
  corm_backend_type_t backend = db->backend->type;

  for (int m = 0; m < count; m++) {
    corm_model_t *model = models[m];

    /* Create table if not exists (idempotent) */
    create_table(db, model, backend);

    /* Introspect existing schema */
    corm_table_info_t info;
    memset(&info, 0, sizeof(info));
    corm_err_t err = db->backend->describe_table
                         ? db->backend->describe_table(db, model->table_name, &info)
                         : CORM_ERR_UNSUPPORTED;

    for (int i = 0; i < model->field_count; i++) {
      corm_field_t *f = &model->fields[i];
      if (f->flags & CORM_FLAG_PRIMARY) continue;  /* already exists */

      /* Column missing → ADD COLUMN */
      if (err == CORM_OK && info.columns && !column_exists(&info, f->name)) {
        char type_buf[64];
        corm_dialect_type_name_str(backend, f->type, f->size, type_buf,
                                   sizeof(type_buf));

        const char *lq = corm_dialect_quote(backend, f->name);
        const char *tq = corm_dialect_quote(backend, model->table_name);
        char alter_sql[512];
        snprintf(alter_sql, sizeof(alter_sql),
                 "ALTER TABLE %s%s%s ADD COLUMN %s%s%s %s;",
                 tq, model->table_name, tq, lq, f->name, lq, type_buf);
        corm_exec(db, alter_sql);  /* ignore error if column already exists */
      }

      /* Create index for INDEX-flagged fields */
      if (f->flags & CORM_FLAG_INDEX) {
        if (err == CORM_OK && info.index_names &&
            index_exists(&info, model->table_name, f->name))
          continue;  /* index already exists */

        const char *lq = corm_dialect_quote(backend, f->name);
        const char *tq = corm_dialect_quote(backend, model->table_name);
        char idx_name[128];
        snprintf(idx_name, sizeof(idx_name), "idx_%s_%s",
                 model->table_name, f->name);
        char idx_sql[512];
        snprintf(idx_sql, sizeof(idx_sql),
                 "CREATE INDEX IF NOT EXISTS %s ON %s%s%s (%s%s%s);",
                 idx_name, tq, model->table_name, tq, lq, f->name, lq);
        corm_exec(db, idx_sql);
      }
    }

    if (err == CORM_OK)
      corm_table_info_free(&info);
  }

  return CORM_OK;
}
```

- [ ] **Step 4: Build and verify**

```bash
cmake -B build && cmake --build build -j$(nproc) 2>&1 | grep -c "error\|warning"
```
Expected: 0.

- [ ] **Step 5: Commit**

```bash
git add src/migration.c
git commit -m "feat(migration): schema-diff aware auto_migrate with index creation"
```


### Task 4: Add index and FK DDL tests

**Files:**
- Modify: `tests/test_migration.c`

- [ ] **Step 1: Add test for index creation on CORM_FLAG_INDEX fields**

```c
static void test_index_creation(void) {
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    TEST("index creation on CORM_FLAG_INDEX");
    printf("SKIP (no sqlite3 backend)\n"); PASS(); return;
  }

  corm_field_t fields[] = {
    {.name = "id", .type = CORM_INT, .offset = 0, .size = sizeof(int),
     .flags = CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC},
    {.name = "email", .type = CORM_STRING, .offset = 4, .size = 128,
     .flags = CORM_FLAG_INDEX, .fk_ref = NULL},
    {.name = "name", .type = CORM_STRING, .offset = 132, .size = 64},
  };
  corm_model_t model = {
    .table_name = "test_index", .struct_size = 196,
    .fields = fields, .field_count = 3, .primary_key = &fields[0],
  };
  corm_model_t *models[] = {&model};

  /* First call: creates table + index */
  err = corm_auto_migrate(db, models, 1);
  assert(err == CORM_OK);

  /* Verify index was created */
  corm_result_t *res = NULL;
  corm_raw(db, "SELECT name FROM sqlite_master WHERE type='index' AND name='idx_test_index_email'", &res);
  assert(res != NULL);
  assert(res->row_count == 1);
  corm_result_release(res);

  /* Second call: re-migrate (should be no-op) */
  err = corm_auto_migrate(db, models, 1);
  assert(err == CORM_OK);

  corm_close(db);
  PASS();
}
```

- [ ] **Step 2: Add FK DDL test**

```c
static void test_foreign_key_reference(void) {
  corm_t *db = NULL;
  corm_err_t err = corm_open("sqlite3://:memory:", &db);
  if (err != CORM_OK) {
    TEST("FK constraint in CREATE TABLE");
    printf("SKIP (no sqlite3 backend)\n"); PASS(); return;
  }

  /* Enable FK enforcement */
  corm_exec(db, "PRAGMA foreign_keys = ON");

  corm_field_t group_fields[] = {
    {.name = "id", .type = CORM_INT, .offset = 0, .size = sizeof(int),
     .flags = CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC},
    {.name = "name", .type = CORM_STRING, .offset = 4, .size = 64},
  };
  corm_model_t group_model = {
    .table_name = "groups", .struct_size = 68,
    .fields = group_fields, .field_count = 2, .primary_key = &group_fields[0],
  };

  corm_field_t user_fields[] = {
    {.name = "id", .type = CORM_INT, .offset = 0, .size = sizeof(int),
     .flags = CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC},
    {.name = "group_id", .type = CORM_INT, .offset = 4, .size = sizeof(int),
     .flags = 0, .fk_ref = "groups.id"},
    {.name = "name", .type = CORM_STRING, .offset = 8, .size = 64},
  };
  corm_model_t user_model = {
    .table_name = "users", .struct_size = 72,
    .fields = user_fields, .field_count = 3, .primary_key = &user_fields[0],
  };

  corm_model_t *models[] = {&group_model, &user_model};
  err = corm_auto_migrate(db, models, 2);
  assert(err == CORM_OK);

  /* Verify: inserting into groups works */
  corm_exec(db, "INSERT INTO groups (name) VALUES ('admins')");
  /* INSERT into users with valid FK */
  err = corm_exec(db, "INSERT INTO users (group_id, name) VALUES (1, 'alice')");
  assert(err == CORM_OK);
  /* INSERT with invalid FK should fail */
  err = corm_exec(db, "INSERT INTO users (group_id, name) VALUES (999, 'bob')");
  assert(err != CORM_OK);  /* FK violation */

  corm_close(db);
  PASS();
}
```

- [ ] **Step 3: Register both tests in main()**

```c
  test_index_creation();
  test_foreign_key_reference();
```

- [ ] **Step 4: Build and verify**

```bash
cmake -B build && cmake --build build -j$(nproc) && ctest --output-on-failure
```
Expected: 15/15 tests pass (test_migration covers both new tests).

- [ ] **Step 5: Commit**

```bash
git add tests/test_migration.c
git commit -m "test(migration): add index creation and FK constraint tests"
```

### Task 5: FK REFERENCES in CREATE TABLE + ALTER TABLE

**Files:**
- Modify: `src/migration.c` (create_table and auto_migrate ADD COLUMN)
- Modify: `src/dialect.c` / `include/corm/dialect.h` (add `corm_dialect_fk_on_delete`)

- [ ] **Step 1: Add FK clause generation helper**

In `src/migration.c` (or a new helper), generate REFERENCES clause from `fk_ref`:
```c
static void append_fk_clause(corm_strbuf_t *sql, corm_backend_type_t backend,
                              corm_field_t *field) {
  if (!field->fk_ref) return;
  /* fk_ref format: "other_table.other_column" */
  const char *dot = strchr(field->fk_ref, '.');
  if (!dot) return;

  size_t table_len = (size_t)(dot - field->fk_ref);
  corm_strbuf_append(sql, " REFERENCES ");
  const char *tq = corm_dialect_quote(backend, field->fk_ref);
  corm_strbuf_appendn(sql, field->fk_ref, table_len);
  /* ... rest of identifier */
}
```

Actually, this is getting complex with quoted identifiers splitting on `.`. Let me simplify: `fk_ref` stores `"tablename.columnname"` and the helper parses the dot to quote each part separately.

Or even simpler: since `fk_ref` is a user-provided string, just wrap the whole thing: `REFERENCES "tablename.columnname"` — but SQLite won't accept that (dot inside quotes is literal). Need to split.

- [ ] **Step 2: Integrate FK into create_table's column loop**

In `create_table()`, after the type name and NOT NULL/USING, append FK clause:
```c
  if (f->fk_ref) {
    char fk_buf[256];
    /* Parse "table.column" */
    const char *dot = strchr(f->fk_ref, '.');
    if (dot) {
      size_t tlen = (size_t)(dot - f->fk_ref);
      const char *tq = corm_dialect_quote(backend, f->fk_ref); /* just get quote char */
      char buf[256];
      int n = snprintf(buf, sizeof(buf), " REFERENCES %.*s%s%s.%s%s",
                       (int)tlen, f->fk_ref,
                       /* quote column part: */
                       corm_dialect_quote(backend, dot + 1), dot + 1,
                       corm_dialect_quote(backend, dot + 1));
      // Hmm this is getting tangled. Let me use a cleaner approach.
    }
  }
```

**Simpler approach:** Two helper functions:
```c
/* Quote a single identifier part, return pointer to static buffer */
static const char *qident(corm_backend_type_t bt, const char *id) {
  static __thread char buf[256];  /* or accept buffer param */
  // ...
}

/* Parse "table.col" and append REFERENCES "table"("col") */
static void build_fk(corm_strbuf_t *sql, corm_backend_type_t bt,
                     const char *fk_ref) {
  const char *dot = strchr(fk_ref, '.');
  if (!dot) return;
  size_t tlen = (size_t)(dot - fk_ref);
  const char *tq = corm_dialect_quote(bt, NULL);
  corm_strbuf_append(sql, " REFERENCES ");
  corm_strbuf_append(sql, tq);
  corm_strbuf_appendn(sql, fk_ref, tlen);
  corm_strbuf_append(sql, tq);
  corm_strbuf_append(sql, " (");
  corm_strbuf_append(sql, tq);
  corm_strbuf_append(sql, dot + 1);
  corm_strbuf_append(sql, tq);
  corm_strbuf_append(sql, ")");
}
```

- [ ] **Step 3: Integrate FK into ADD COLUMN path**

FK constraints on ADD COLUMN are limited in SQLite (can't add FK via ALTER TABLE). So for ADD COLUMN, ignore fk_ref — FK is only created in the initial CREATE TABLE. Document this limitation.

Actually, SQLite does support ADD COLUMN with REFERENCES, but the constraint is not enforced ("foreign key constraints are not automatically created when adding a column"). So we skip FK on ALTER ADD COLUMN and rely on CREATE TABLE for FK creation. This is a SQLite limitation, not ours.

- [ ] **Step 4: Build and verify**

```bash
cmake -B build && cmake --build build -j$(nproc) && ctest --output-on-failure
```
Expected: 0 warnings, at least 15/15 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/migration.c
git commit -m "feat(migration): add FOREIGN KEY REFERENCES clause in CREATE TABLE"
```


## Verification

| Gate | Command | Expected |
|------|---------|----------|
| All warnings | `cmake -B build -DCMAKE_C_FLAGS="-Wall -Wextra -Wunused -Werror" && cmake --build build -j$(nproc)` | 0 warnings |
| All tests | `ctest --output-on-failure` | 16/16 or more tests pass |
| LSP clean | `lsp_diagnostics` on changed files | 0 errors |
