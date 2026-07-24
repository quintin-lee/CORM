# CORM Architecture

Internal architecture of the CORM database adapter. This document is for contributors who want to understand or extend the codebase.

## Overview

CORM is a lightweight GORM-inspired ORM/query builder for C. It provides:

- Model-to-table mapping via struct field descriptors
- A fluent query builder that assembles SQL from fragments
- Backend abstraction via vtable dispatch (SQLite3, MySQL, PostgreSQL)
- Auto-migration from model definitions
- Result sets with typed accessors and reference counting

**Public API surface**: `include/corm/corm.h` — include this header (or individual headers from `include/corm/`) for the full API.

## Directory Structure

```
crom/
├── include/corm/            # Public headers
│   ├── corm.h               # Aggregate header (includes all below)
│   ├── types.h              # Error codes, corm_value_t tagged union
│   ├── model.h              # Model/field definitions, CORM_FIELD macro
│   ├── query.h              # Query builder API
│   ├── result.h             # Result set iteration & typed accessors
│   ├── config.h             # Connection configuration
│   ├── backend.h            # Backend vtable type + registration
│   └── pool.h               # Connection pool API
├── src/
│   ├── corm.c               # Core: DSN parsing, open/close/exec/raw/init
│   ├── model.c              # Model registry, field value mapping
│   ├── query.c              # Query builder, execution, convenience APIs
│   ├── result.c             # Result set management, scalar helpers
│   ├── migration.c          # Auto-migration (CREATE TABLE)
│   ├── builder.c            # SQL generation from query state
│   ├── dialect.c            # Backend-specific SQL fragments
│   ├── internal/            # Private implementation details
│   │   ├── corm_internal.h  # struct corm definition, registry type
│   │   ├── strbuf.h         # Dynamic string buffer
│   │   ├── hash.h           # String-keyed hash table (djb2)
│   │   ├── pool.h / pool.c  # Connection pool internals
│   │   └── stmt_cache.h / c # LRU prepared statement cache
│   └── backend/             # Database drivers
│       ├── backend.c        # Registry (array of corm_backend_t*)
│       ├── sqlite3.c        # SQLite3 driver
│       ├── mysql.c          # MySQL driver (prepared statements)
│       └── postgres.c       # PostgreSQL driver (PQexecParams)
├── tests/                   # 14 test suites
├── examples/                # Usage examples
└── docs/                    # Design documents
```

## Core Data Flow

```
User Code
    │
    ▼
corm_open("sqlite3://:memory:", &db)        ← DSN parsing → backend selection
    │
    ▼
corm_register_model(db, &user_model)         ← Hash table insert (table_name → model)
    │
    ▼
corm_auto_migrate(db, models[], n)           ← CREATE TABLE per model
    │
    ▼
corm_query_new(db, model)                    ← Allocate query state (strbufs for fragments)
    │
    ├─ corm_query_select(q, "name")
    ├─ corm_query_where(q, "age > ?")
    ├─ corm_query_bind(q, {.v.i = 18})
    ├─ corm_query_order(q, "name ASC")
    │
    ▼
corm_find(q, &result)                        ← corm_build_sql() → backend->query()
    │
    ▼
corm_result_next(r) / corm_result_int()      ← Iterate rows, typed access
```

## Key Components

### 1. Connection (`corm.c`)

**DSN Parsing**: Minimal URI parser recognizes `sqlite3://`, `sqlite://`, `mysql://`, `postgres://`, `postgresql://`. Default is SQLite3. The DSN format is `<backend>://<path>` where path is the database filename (SQLite) or connection string (MySQL/PostgreSQL).

**struct corm** (defined in `internal/corm_internal.h`):
```c
struct corm {
    corm_backend_t *backend;     // Vtable for current backend
    void *conn;                  // Backend-specific handle (sqlite3*/PGconn*)
    corm_config_t config;        // Connection pool settings
    corm_registry_t registry;    // Model lookup tables
    corm_err_t last_err;         // Last error code
    char err_msg[512];           // Error message string
    char err_sql[1024];          // SQL that caused the error
    int64_t last_insert_id_val;  // Cached last_insert_id
    int rows_affected_val;       // Cached rows_affected
};
```

The `last_insert_id_val` and `rows_affected_val` fields cache values from the last exec/query call so they can be retrieved later via `backend->last_insert_id()` and `backend->rows_affected()`.

### 2. Model Registry (`model.c`)

Uses two hash tables (via `internal/hash.h`) for O(1) lookup:
- `models_by_table`: table_name → corm_model_t*
- `models_by_name`: struct_type_name → corm_model_t* (reserved)

**Field Value Mapping**: `corm_field_get_value()` and `corm_field_set_value()` use `field->offset` (from `offsetof()`) to compute the address of each field within a struct instance. Values are packed into `corm_value_t` unions before passing to the backend layer.

Memory ownership:
- **Registry does NOT own** `corm_model_t` and `corm_field_t` memory — these are typically static or stack-allocated by the user
- Registry DOES own hash entry keys (strdup'd) and entries themselves

### 3. Query Builder (`query.c`)

Assembles SQL from fragments stored in `corm_strbuf_t` members:

| Member | Purpose |
|---|---|
| `select_cols` | Column list for SELECT |
| `where` | WHERE condition (AND/OR chaining) |
| `order` | ORDER BY clause |
| `group` | GROUP BY clause |
| `having` | HAVING condition |
| `joins` | JOIN clauses |
| `set_clause` | SET assignments for UPDATE |
| `params[]` | Bound parameter values (dynamic array) |

**Execution flow** (e.g., `corm_find`):
1. Call `corm_build_sql(q, &sql, backend_type)` to assemble complete SQL
2. Call `backend->query(db, sql_cstr, params, param_count, &result)`
3. Backend fills `corm_result_t` with column metadata and row data

**Design note**: `corm_query_where()` and `corm_query_or_where()` accept variadic args in signature but do NOT process them — users must use `corm_query_bind()` for parameterized conditions. This was a deliberate fix after a UB bug was discovered.

### 4. SQL Builder (`builder.c`)

`corm_build_sql()` generates SQL from query state. Key design decisions:

- **Identifier quoting**: All table and field names are wrapped via `qident()` which calls `corm_dialect_quote()` — prevents SQL injection on structural elements
- **Placeholder strategy**: INSERT/UPDATE use `?` (SQLite/MySQL) or `$1, $2` (PostgreSQL) based on `corm_dialect_placeholder(bt, index)`
- **LIMIT/OFFSET**: All backends use inline literal integers — limits and offsets are always set programmatically, never from user input, so there is no injection risk. This avoids PostgreSQL `$n` placeholder complexity.
- **UPDATE SET**: Two paths — if `set_clause` has content, it scans for `?` and replaces with dialect placeholders; otherwise iterates non-PK fields

### 5. Migration (`migration.c`)

`corm_auto_migrate()` calls `create_table()` for each model. The generated SQL:

1. Starts with `CREATE TABLE IF NOT EXISTS <quoted_table_name> (`
2. For each field:
   - Quoted column name + dialect type name
   - If AUTOINC: uses `corm_dialect_autoinc()` (e.g., `SERIAL PRIMARY KEY` for PG, `AUTO_INCREMENT` for MySQL, `INTEGER PRIMARY KEY AUTOINCREMENT` for SQLite)
   - If NOT_NULL: appends `NOT NULL`
   - If UNIQUE: appends `UNIQUE`
   - If default_value set: appends `DEFAULT <value>`
   - If PRIMARY KEY (non-autoinc): appends `PRIMARY KEY`
3. Ends with `)`

Currently only supports CREATE TABLE — no ALTER TABLE for schema evolution.

### 6. Dialect Abstraction (`dialect.c`)

Provides backend-specific SQL fragments:

| Function | SQLite | MySQL | PostgreSQL |
|---|---|---|---|
| `corm_dialect_quote()` | `"` | `` ` `` | `"` |
| `corm_dialect_placeholder()` | `?` | `?` | `$n` |
| `corm_dialect_autoinc()` | `INTEGER PRIMARY KEY AUTOINCREMENT` | `AUTO_INCREMENT` | `SERIAL PRIMARY KEY` |
| `corm_dialect_limit_offset()` | `LIMIT %d OFFSET %d` (all backends use literal integers) | | |
| `corm_dialect_if_not_exists()` | `IF NOT EXISTS` (all) | `IF NOT EXISTS` (all) | `IF NOT EXISTS` (all) |
| `corm_dialect_type_name()` | INTEGER/REAL/TEXT/BLOB | INT/BIGINT/FLOAT/VARCHAR(n)/TEXT/BLOB/TINYINT(1) | INTEGER/BIGINT/REAL/DOUBLE PRECISION/VARCHAR(n)/TEXT/BYTEA/BOOLEAN |

### 7. Backend Vtable Pattern

Each backend implements `corm_backend_t`:

```c
typedef struct corm_backend {
    const char *name;
    corm_backend_type_t type;
    corm_err_t (*open)(corm_t*, const char*);
    corm_err_t (*close)(corm_t*);
    corm_err_t (*ping)(corm_t*);
    corm_err_t (*exec)(corm_t*, const char*, corm_value_t*, int);
    corm_err_t (*query)(corm_t*, const char*, corm_value_t*, int, corm_result_t**);
    corm_err_t (*begin)(corm_t*);
    corm_err_t (*commit)(corm_t*);
    corm_err_t (*rollback)(corm_t*);
    size_t (*escape_string)(corm_t*, char*, const char*, size_t);
    int64_t (*last_insert_id)(corm_t*);
    int (*rows_affected)(corm_t*);
} corm_backend_t;
```

**Registration**: Each backend file defines a static `corm_backend_t` instance and registers it via `__attribute__((constructor))`. For environments without constructors, explicit `corm_register_<backend>_backend()` functions are provided.

**Stub backends**: When a backend library is not found at build time, stub implementations with NULL function pointers are compiled in. `corm_open_with_config()` checks `db->backend->open != NULL` before calling.

#### Backend-Specific Notes

**SQLite3** (`backend/sqlite3.c`):
- Uses `sqlite3_prepare_v2` + `sqlite3_step` pattern (not `sqlite3_exec` for queries)
- Query results are materialized in two passes: first counts rows, then fetches all data
- Column types determined during first row fetch (after `sqlite3_reset`, column type info is lost)
- WAL mode enabled for better concurrency
- Foreign keys enabled via PRAGMA

**MySQL** (`backend/mysql.c`):
- Uses `mysql_stmt_prepare` + `mysql_stmt_bind_param`/`mysql_stmt_bind_result` for prepared statements
- Parameter binding converts `corm_value_t` to MYSQL_BIND structures
- Result fetching uses `mysql_stmt_fetch` with column buffers

**PostgreSQL** (`backend/postgres.c`):
- Uses `PQprepare` + `PQexecParams` for prepared statements
- Parameters passed as `const char**` array with type OIDs
- `PQcmdTuples()` captured after exec/query to implement `rows_affected()`
- Memory leak fixed: `char **tmp_strs` array tracks all strdup'd param strings, freed after PQexecParams

### 8. Result Set (`result.c`)

**Reference counting**: `corm_result_t` has a `refcount` field. Created with refcount=1, incremented by `corm_result_retain()`, decremented by `corm_result_release()`. When refcount reaches 0, all owned memory (row data, column names, strings, blobs) is freed.

**Row storage**: `corm_value_t** rows` — each row is an array of `corm_value_t`. Text and blob values are heap-allocated (strdup/malloc) and freed during release. Integer/double/bool values are stored inline in the union.

**Column type detection**: Backends populate `column_types[]` during result creation. SQLite determines types from first row's `sqlite3_column_type()` because post-reset returns SQLITE_NULL.

### 9. Soft Delete (`query.c` / `model.h`)

The soft delete mechanism marks rows as deleted without physical removal:

1. **Field flag**: A model field tagged with `CORM_FLAG_SOFT_DELETE` identifies the "deleted at" timestamp column.
2. **Delete conversion**: `corm_delete()` detects the flag and generates `UPDATE table SET deleted_at = <now> WHERE ...` instead of `DELETE FROM`.
3. **Auto-filtering**: `corm_find()` and `corm_first()` automatically append `AND <soft_delete_col> IS NULL` to the WHERE clause.
4. **Unscoped bypass**: `corm_query_unscoped(q)` clears the soft-delete filter, allowing queries to see soft-deleted rows.

The auto-filter is applied in `corm_build_sql()` when:
- The model has a field with `CORM_FLAG_SOFT_DELETE`
- `corm_query_unscoped()` has NOT been called on the query
- The operation is SELECT (via `corm_find`/`corm_first`) or DELETE (via `corm_delete`)

### 10. Hooks (`query.c`)

Model hooks provide lifecycle callbacks for CRUD operations. Hooks are defined as function pointers in `corm_model_t`:

| Hook | Signature | Triggered by |
|---|---|---|
| `before_create` | `corm_err_t (*)(corm_t*, corm_model_t*, void*)` | `corm_create()` / `corm_create_one()` |
| `after_create` | `corm_err_t (*)(corm_t*, corm_model_t*, void*)` | After successful INSERT |
| `before_update` | `corm_err_t (*)(corm_t*, corm_model_t*, void*)` | `corm_update()` |
| `after_update` | `corm_err_t (*)(corm_t*, corm_model_t*, void*)` | After successful UPDATE |
| `before_delete` | `corm_err_t (*)(corm_t*, corm_model_t*, void*)` | `corm_delete()` |
| `after_delete` | `corm_err_t (*)(corm_t*, corm_model_t*, void*)` | After successful DELETE |
| `after_find` | `corm_err_t (*)(corm_t*, corm_model_t*, void*)` | After row data is read into struct |

**Execution flow**: Hooks run in the order: `before_*` → execute SQL → `after_*`. If a `before_*` hook returns non-OK, the SQL execution is skipped. The `void*` argument is the record being operated on, cast to the model's struct type.

### 11. Connection Pool (`internal/pool.c` / `include/corm/pool.h`)

The connection pool manages reuse of database connections with high-performance concurrent locking:

- **Public API**: `corm_pool_create()`, `corm_pool_acquire()`, `corm_pool_release()`, `corm_pool_destroy()`
- **Max connections**: Guarded by `max_open_conns` in `corm_config_t`. Reaching the limit causes `acquire` to wait or timed-wait on a condition variable.
- **Idle management**: `max_idle_conns` controls how many connections remain open after release. Excess idle connections are closed.
- **Lifetime limit**: `conn_max_lifetime_ms` — connections older than this are closed and replaced on next acquire.
- **Lock Granularity Optimization**: All slow network and disk I/O operations (`corm_ping`, connection creation, and retry sleeps) are executed *outside* the pool mutex. Slots are atomically reserved/released under lock, ensuring ultra-low lock contention.
- **Leak-Free Safety**: Node memory allocation failures during release trigger immediate connection teardown and counter decrements to prevent resource leaks.

`corm_open_with_config()` internally uses the pool when `max_open_conns > 0`.

### 12. Prepared Statement Cache (`internal/stmt_cache.c`)

The statement cache stores backend-specific prepared statement handles keyed by SQL text:

- **LRU eviction**: A doubly-linked list tracks usage order. When `max_capacity` is reached, the least recently used entry is evicted.
- **Thread Safety**: Access to the cache (`get`, `put`, `remove`, `destroy`) is protected by a `pthread_mutex_t` mutex.
- **Hash table / Linked list**: Double linked-list combined with string key lookup provides O(1) LRU promotion and eviction.

### 13. Logger (`corm.c`)

The pluggable logger intercepts all SQL execution events:

- **Installation**: `corm_set_logger(db, callback_fn, user_data)`
- **Callback type**: `void (*)(void *user_data, corm_log_level_t level, const char *sql, int64_t elapsed_us, corm_err_t err)`
- **Log levels**: `CORM_LOG_DEBUG`, `CORM_LOG_INFO`, `CORM_LOG_WARN`, `CORM_LOG_ERROR`
- **Timing**: `elapsed_us` measures microseconds between SQL submission and completion
- **Coverage**: Both `corm_exec()` and `corm_raw()` invoke the logger. All backend exec/query operations are timed.

### 14. Internal Utilities

**strbuf.h** — Dynamic string buffer for SQL assembly:
- Exponential growth (doubling from initial 64 bytes)
- `append()`, `appendn()`, `appendf()` (printf-style)
- `clear()` resets length to 0 without freeing buffer
- Used by query builder fragments and SQL builder

**hash.h** — Simple string-keyed hash table:
- djb2 hash algorithm
- Separate chaining for collision resolution
- Buckets initialized to size 32
- Used by model registry (table_name → model mapping)

## Adding a New Backend

1. Create `src/backend/<name>.c`
2. Implement all `corm_backend_t` function pointers
3. Define a static `corm_backend_t` instance with `.name` and `.type`
4. Add `__attribute__((constructor))` registration OR expose `corm_register_<name>_backend()`
5. Add `#ifdef CORM_HAVE_<NAME>` guards for optional compilation
6. Update `CMakeLists.txt`:
   - Find dependency (find_library/find_package)
   - Add `-DCORM_HAVE_<NAME>` definition when found
   - Add source file to `CORM_SOURCES`
   - Link library to `corm` target
7. Update `include/corm/backend.h`: add `CORM_BACKEND_<NAME>` to `corm_backend_type_t` enum
8. Update `dialect.c`: add case for new backend in `corm_dialect_quote()`, `corm_dialect_placeholder()`, etc.
9. Write tests in `tests/test_<name>.c`

## Design Decisions

### Why vtable dispatch over inheritance?
C doesn't have OOP. The vtable pattern (function pointer struct) provides polymorphism with minimal overhead — just one pointer dereference per backend call.

### Why struct offsets instead of reflection?
C has no runtime reflection. `offsetof()` computed at compile time gives zero-cost field access. The `CORM_FIELD` macro hides the boilerplate.

### Why two-pass query materialization (SQLite)?
SQLite's `sqlite3_column_type()` returns meaningful data only while the statement is stepped. After `sqlite3_reset()`, all columns return SQLITE_NULL. So we step once to count rows and detect types, then reset and step again to collect data.

### Why no variadic params in where()?
Early versions used `va_arg(ap, corm_value_t)` which caused undefined behavior when called with no variadic arguments. The fix was to remove the variadic body entirely and require explicit `corm_query_bind()` calls. This is safer and more explicit.

### Why static linking for backends?
Shared library backends would require `dlopen`/`dlsym` and `.so` deployment. Static linking with `__attribute__((constructor))` auto-registers backends at load time. Users link `libcorm.a` and get all available backends automatically.
