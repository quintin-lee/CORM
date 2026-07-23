# CORM — C Database Adapter

A lightweight, GORM-inspired ORM/query builder for C with multi-backend support (SQLite3, MySQL, PostgreSQL).

## Features

- **Multi-backend**: SQLite3, MySQL, PostgreSQL via vtable dispatch
- **Auto-migration**: Create tables from model definitions (CREATE TABLE IF NOT EXISTS)
- **Query builder**: Fluent API for SELECT/INSERT/UPDATE/DELETE with parameterized queries preventing SQL injection
- **Model hooks**: Before/after callbacks for Create/Update/Delete/Find operations
- **Soft delete**: Mark rows as deleted without physical removal, auto-filtered on queries
- **Connection pool**: Reuse connections with configurable max count, idle count, and lifetime
- **Prepared statement cache**: LRU cache with configurable TTL and capacity
- **Type-safe records**: Field offsets computed at compile time, automatic value mapping via `corm_value_t` tagged union
- **Transaction support**: BEGIN/COMMIT/ROLLBACK + named savepoints
- **Result iterator**: Forward-only cursor with typed accessors and reference counting
- **Dialect-aware**: Identifier quoting, placeholder format, type mapping, LIMIT/OFFSET per backend
- **Pluggable logger**: Intercept all SQL execution with timing, error codes, and log levels
- **Batch insert**: Insert multiple rows in a single SQL statement
- **Static linking**: Backend registration via `__attribute__((constructor))` or explicit calls

## Project Structure

```
crom/
├── CMakeLists.txt              # Build configuration
├── .clang-format               # Code formatting standard (LLVM style)
├── include/corm/               # Public headers
│   ├── corm.h                  # Aggregate header (includes all below)
│   ├── types.h                 # Error codes, corm_value_t tagged union
│   ├── model.h                 # Model/field definitions, CORM_FIELD macro
│   ├── query.h                 # Query builder API
│   ├── result.h                # Result set iteration & typed accessors
│   ├── config.h                # Connection configuration struct
│   ├── backend.h               # Backend vtable type
│   └── pool.h                  # Connection pool API
├── src/                        # Implementation
│   ├── corm.c                  # Core: DSN parsing, open/close/tx/exec/raw
│   ├── model.c                 # Model registry & field value helpers
│   ├── query.c                 # Query builder & execution
│   ├── builder.c               # SQL generation from query state
│   ├── dialect.c               # Backend-specific SQL fragments
│   ├── migration.c             # Auto-migration (CREATE TABLE)
│   ├── result.c                # Result set management
│   ├── internal/               # Internal implementation details
│   │   ├── corm_internal.h     # struct corm definition, registry type
│   │   ├── strbuf.h            # Dynamic string buffer
│   │   ├── hash.h              # String-keyed hash table (djb2)
│   │   ├── pool.c / pool.h     # Connection pool internals
│   │   ├── stmt_cache.c / .h   # Prepared statement LRU cache
│   │   └── ──
│   └── backend/                # Database drivers
│       ├── backend.c           # Registry
│       ├── sqlite3.c           # SQLite3 driver
│       ├── mysql.c             # MySQL driver (prepared statements)
│       └── postgres.c          # PostgreSQL driver (PQexecParams)
├── tests/                      # 14 test suites
├── examples/                   # Usage examples
└── docs/                       # Design documents
```

## Quick Start

### Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
ctest
```

### Minimal Example

```c
#include <corm/corm.h>

typedef struct {
    int id;
    char name[256];
} User;

static corm_field_t user_fields[] = {
    CORM_FIELD(User, id,   CORM_INT,    CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC, NULL),
    CORM_FIELD(User, name, CORM_STRING, CORM_FLAG_NOT_NULL, NULL),
};

static corm_model_t user_model = {
    .table_name  = "users",
    .struct_size = sizeof(User),
    .fields      = user_fields,
    .field_count = 2,
    .primary_key = &user_fields[0],
};

int main(void) {
    corm_t *db;
    corm_open("sqlite3://:memory:", &db);

    corm_register_model(db, &user_model);
    corm_model_t *models[] = { &user_model };
    corm_auto_migrate(db, models, 1);

    User u = { .name = "Alice" };
    corm_create_one(db, &user_model, &u, NULL);

    corm_close(db);
    return 0;
}
```

## DSN Format

```
<backend>://<path>
```

| Backend | Example DSN | Notes |
|---------|------------|-------|
| SQLite3 | `sqlite3://:memory:` | `:memory:` for in-memory DB |
| SQLite3 | `sqlite3:///path/to/db.sqlite` | File path |
| MySQL | `mysql://user:pass@host:3306/dbname` | Connection string |
| PostgreSQL | `postgres://user:pass@host:5432/dbname` | Connection string |

Supported scheme prefixes: `sqlite3://`, `sqlite://`, `mysql://`, `postgres://`, `postgresql://`.

## API Reference

### Connection

| Function | Description |
|---|---|
| `corm_open(dsn, &db)` | Open with default config |
| `corm_open_with_config(dsn, config, &db)` | Open with custom config |
| `corm_close(db)` | Close connection |
| `corm_ping(db)` | Health check |
| `corm_get_backend(db)` | Return backend type enum |

### Raw Execution

| Function | Description |
|---|---|
| `corm_exec(db, sql)` | Execute raw SQL (no result set) |
| `corm_raw(db, sql, &result)` | Execute query, get result set |

### Transactions

| Function | Description |
|---|---|
| `corm_begin(db)` | Begin transaction |
| `corm_commit(db)` | Commit transaction |
| `corm_rollback(db)` | Rollback transaction |
| `corm_savepoint(db, name)` | Create named savepoint |
| `corm_rollback_to(db, name)` | Rollback to savepoint |
| `corm_release_savepoint(db, name)` | Release savepoint |

### Logger

| Function | Description |
|---|---|
| `corm_set_logger(db, fn, user_data)` | Install logger callback |

Logger callback signature: `void (*)(void *user_data, corm_log_level_t level, const char *sql, int64_t elapsed_us, corm_err_t err)`.

### Migration

| Function | Description |
|---|---|
| `corm_auto_migrate(db, models[], count)` | Auto-create tables from model definitions |

### Query Builder

**Construction / lifecycle:**

| Function | Description |
|---|---|
| `corm_query_new(db, model)` | Create new query for a model |
| `corm_query_free(q)` | Free query resources |
| `corm_query_reset(q)` | Reset query state for reuse |

**Clause builders:**

| Function | Description |
|---|---|
| `corm_query_select(q, cols)` | SELECT columns |
| `corm_query_where(q, cond)` | WHERE condition |
| `corm_query_or_where(q, cond)` | OR WHERE condition |
| `corm_query_where_in(q, col, vals, n)` | WHERE col IN (...) |
| `corm_query_where_between(q, col, a, b)` | WHERE col BETWEEN a AND b |
| `corm_query_where_null(q, col)` | WHERE col IS NULL |
| `corm_query_where_not_null(q, col)` | WHERE col IS NOT NULL |
| `corm_query_join(q, join_type, table, on)` | JOIN clause |
| `corm_query_order(q, expr)` | ORDER BY clause |
| `corm_query_group(q, expr)` | GROUP BY clause |
| `corm_query_having(q, cond)` | HAVING condition |
| `corm_query_limit(q, n)` | LIMIT |
| `corm_query_offset(q, n)` | OFFSET |
| `corm_query_set(q, field, val)` | SET field = ? (for UPDATE) |
| `corm_query_set_raw(q, expr)` | SET expression (raw, no quoting) |
| `corm_query_bind(q, val)` | Bind a parameter value (corm_value_t) |
| `corm_query_unscoped(q)` | Disable soft-delete filter |
| `corm_query_preload(q, relation)` | Register relation for preloading (no-op / reserved) |

**Execution:**

| Function | Description |
|---|---|
| `corm_find(q, &result)` | Execute SELECT, get result set |
| `corm_first(q, record)` | Fetch first row into struct |
| `corm_create(q, record, &id)` | INSERT |
| `corm_update(q, &affected)` | UPDATE (returns rows affected) |
| `corm_delete(q, &affected)` | DELETE (returns rows affected) |
| `corm_create_batch(db, model, records, count)` | Batch INSERT |

### High-Level Convenience

| Function | Description |
|---|---|
| `corm_create_one(db, model, record, &id)` | Insert one record |
| `corm_find_all(db, model, where, records, &count)` | Find all matching rows |

### Result Set Accessors

| Function | Description |
|---|---|
| `corm_result_next(r)` | Advance to next row |
| `corm_result_reset(r)` | Reset to before first row |
| `corm_result_row_count(r)` | Number of rows |
| `corm_result_col_count(r)` | Number of columns |
| `corm_result_col_name(r, i)` | Column name at index |
| `corm_result_col_type(r, i)` | Column type at index |
| `corm_result_int(r, i)` | Get int64 value at column |
| `corm_result_double(r, i)` | Get double value at column |
| `corm_result_string(r, i)` | Get string value at column |
| `corm_result_bool(r, i)` | Get bool value at column |
| `corm_result_is_null(r, i)` | Check if column is NULL |
| `corm_result_value(r, i)` | Get raw corm_value_t pointer |
| `corm_result_retain(r)` | Increment reference count |
| `corm_result_release(r)` | Decrement refcount, free at 0 |

### Utility

| Function | Description |
|---|---|
| `corm_init()` | Register all available backends |
| `corm_last_error(db, buf, size)` | Get last error message |
| `corm_err_string(err)` | Get string for error code |
| `corm_register_backend(type, backend)` | Register a custom backend |
| `corm_find_model(db, name)` | Look up model by struct name |
| `corm_find_field(model, name)` | Look up field by name |

## Configuration

### Connection Settings

```c
typedef struct {
    int max_open_conns;        /* Max connections (0 = unlimited)           */
    int max_idle_conns;        /* Max idle connections (default: 2)        */
    int conn_max_lifetime_ms;  /* Connection lifetime (0 = no limit)       */
    int timeout_ms;            /* Connection timeout (default: 30000ms)    */
    bool verbose_logging;      /* Enable debug logging                     */
} corm_config_t;

#define CORM_DEFAULT_CONFIG {0, 2, 0, 30000, false}
```

### Logger

```c
/* Enable per-connection logging */
corm_set_logger(db, my_logger, my_user_data);

/* Log levels */
typedef enum {
    CORM_LOG_DEBUG,
    CORM_LOG_INFO,
    CORM_LOG_WARN,
    CORM_LOG_ERROR,
} corm_log_level_t;
```

The logger receives every SQL execution event with timing information (`elapsed_us`), the SQL text, the result code, and custom user data.

## Error Codes

| Code | Value | Meaning |
|---|---|---|
| `CORM_OK` | 0 | Success |
| `CORM_ERR_GENERIC` | -1 | Generic error |
| `CORM_ERR_NOMEM` | -2 | Out of memory |
| `CORM_ERR_NOTFOUND` | -3 | Record not found |
| `CORM_ERR_DUP` | -4 | Duplicate key |
| `CORM_ERR_BACKEND` | -5 | Backend unavailable |
| `CORM_ERR_TYPE` | -6 | Type mismatch |
| `CORM_ERR_NULL` | -7 | Null pointer |
| `CORM_ERR_BOUNDS` | -8 | Out of bounds |
| `CORM_ERR_MISMATCH` | -9 | Column/field mismatch |

## Model Definition

```c
#define CORM_FIELD(st, fn, ft, fl, def) \
    {.name=#fn, .type=(ft), .offset=offsetof(st,fn), \
     .size=sizeof(((st*)0)->fn), .flags=(fl), .default_value=(def)}
```

### Field Flags

| Flag | Value | Description |
|---|---|---|
| `CORM_FLAG_PRIMARY` | `(1<<0)` | Primary key |
| `CORM_FLAG_AUTOINC` | `(1<<1)` | Auto-increment |
| `CORM_FLAG_NOT_NULL` | `(1<<2)` | NOT NULL constraint |
| `CORM_FLAG_UNIQUE` | `(1<<3)` | UNIQUE constraint |
| `CORM_FLAG_SOFT_DELETE` | `(1<<4)` | Soft delete field |

### Supported Types

| Type | SQLite | MySQL | PostgreSQL |
|---|---|---|---|
| `CORM_INT` | INTEGER | INT | INTEGER |
| `CORM_INT64` | INTEGER | BIGINT | BIGINT |
| `CORM_FLOAT` | REAL | FLOAT | REAL |
| `CORM_DOUBLE` | REAL | DOUBLE | DOUBLE PRECISION |
| `CORM_STRING` | TEXT | VARCHAR(n) | VARCHAR(n) |
| `CORM_TEXT` | TEXT | TEXT | TEXT |
| `CORM_BLOB` | BLOB | BLOB | BYTEA |
| `CORM_BOOL` | INTEGER | TINYINT(1) | BOOLEAN |

## Soft Delete

Mark a field with `CORM_FLAG_SOFT_DELETE` to enable soft deletion:

```c
typedef struct {
    int id;
    char name[64];
    char deleted_at[32];     /* Soft-delete timestamp field */
} Item;

static corm_field_t item_fields[] = {
    CORM_FIELD(Item, id,         CORM_INT,    CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC, NULL),
    CORM_FIELD(Item, name,       CORM_STRING, 0, NULL),
    CORM_FIELD(Item, deleted_at, CORM_STRING, CORM_FLAG_SOFT_DELETE, NULL),
};
```

- `corm_delete()` with a soft-delete model issues an UPDATE (sets `deleted_at`) instead of a DELETE
- `corm_find()` / `corm_first()` automatically appends `WHERE deleted_at IS NULL`
- Call `corm_query_unscoped(q)` before `corm_find()`/`corm_delete()` to bypass the filter

## Connection Pool

The library includes a built-in connection pool:

```c
corm_pool_t *pool = corm_pool_create("sqlite3://:memory:", &config, max_size);
corm_t *db = corm_pool_acquire(pool);
/* ... use db ... */
corm_pool_release(pool, db);
corm_pool_destroy(pool);
```

`corm_open_with_config()` internally uses the pool when `max_open_conns > 0`.

## Backend Registration

Backends auto-register via `__attribute__((constructor))`. For static linking without constructors:

```c
/* Call early in main() */
corm_init();  /* Registers all available backends */
/* Or individually: */
corm_register_sqlite3_backend();
corm_register_mysql_backend();
corm_register_postgres_backend();
```

## Query Builder Usage

```c
corm_query_t *q = corm_query_new(db, &user_model);

corm_query_select(q, "name, age")
    ->query_where(q, "age > ?")
    ->query_bind(q, (corm_value_t){.v.i = 18})
    ->query_order(q, "age DESC")
    ->query_limit(q, 10);

corm_result_t *res = NULL;
corm_find(q, &res);

/* Iterate results */
while (corm_result_next(res)) {
    const char *name = corm_result_string(res, 0);
    int64_t age      = corm_result_int(res, 1);
    printf("%s is %ld\n", name, age);
}

corm_result_release(res);
corm_query_free(q);
```

### Error Handling

```c
corm_t *db;
corm_err_t err = corm_open("sqlite3://:memory:", &db);
if (err != CORM_OK) {
    char buf[256];
    corm_last_error(db, buf, sizeof(buf));
    fprintf(stderr, "open failed: %s\n", buf);
    return 1;
}
```

## Testing

```bash
cd build
ctest --output-on-failure
```

14 test suites:

| Suite | Type | Purpose |
|---|---|---|
| `test_core` | Unit | Connection lifecycle, transactions, error handling |
| `test_model` | Unit | Model registry, field mapping, type helpers |
| `test_query` | Unit | Query builder, SQL generation, placeholders |
| `test_sqlite` | Integration | SQLite backend, type round-trips, NULL handling |
| `test_backend_drivers` | Integration | Driver registration and validation |
| `test_pool` | Integration | Connection pool acquire/release/destroy |
| `test_migration` | Integration | Auto-migration CREATE TABLE |
| `test_hooks` | Integration | Before/after CRUD hook callbacks |
| `test_full_hooks` | Integration | End-to-end hook lifecycle |
| `test_predicates` | Integration | WHERE IN, BETWEEN, NULL, NOT NULL |
| `test_preload` | Integration | Preload scaffolding (no-op) |
| `test_stmt_cache` | Integration | LRU prepared statement cache |
| `test_soft_delete` | Integration | Soft delete mechanism |
| `test_logger` | Integration | Logger callback and timing |

## Build Options

| CMake Option | Default | Description |
|---|---|---|
| `CORM_WITH_SQLITE3` | ON | Build SQLite3 backend |
| `CORM_WITH_MYSQL` | OFF | Build MySQL backend |
| `CORM_WITH_POSTGRES` | OFF | Build PostgreSQL backend |
| `CORM_BUILD_TESTS` | ON | Build test suites |
| `CORM_BUILD_EXAMPLES` | ON | Build example programs |
| `CORM_ENABLE_ASAN` | OFF | Enable AddressSanitizer |
| `CMAKE_BUILD_TYPE` | Debug | Debug / Release / RelWithDebInfo |
