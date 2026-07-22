# CORM — C Database Adapter

A lightweight, GORM-inspired ORM/query builder for C with multi-backend support (SQLite3, MySQL, PostgreSQL).

## Features

- **Multi-backend**: SQLite3, MySQL, PostgreSQL via vtable dispatch
- **Auto-migration**: Create/alter tables from model definitions
- **Query builder**: Fluent API for SELECT/INSERT/UPDATE/DELETE with SQL injection protection
- **Model hooks**: Before/after callbacks for CRUD operations
- **Type-safe records**: Field offsets computed at compile time, automatic value mapping
- **Transaction support**: BEGIN/COMMIT/ROLLBACK
- **Result iterator**: Forward-only cursor with typed accessors
- **Dialect-aware**: Identifier quoting, placeholders, LIMIT/OFFSET per backend
- **Static linking**: Backend registration via `__attribute__((constructor))` or explicit calls

## Project Structure

```
crom/
├── CMakeLists.txt          # Build configuration
├── src/
│   ├── corm_pub.h          # Public API header (all you need)
│   ├── internal/           # Internal implementation
│   │   ├── corm_internal.h
│   │   ├── strbuf.h
│   │   └── hash.h
│   ├── corm.c              # Core: open/close/exec/raw/init
│   ├── model.c             # Model registry & field value helpers
│   ├── query.c             # Query builder & execution
│   ├── result.c            # Result set management
│   ├── migration.c         # Auto-migration (CREATE TABLE)
│   ├── builder.c           # SQL generation from query state
│   ├── dialect.c           # Backend-specific SQL fragments
│   └── backend/            # Database backends
│       ├── backend.c       # Registry
│       ├── sqlite3.c       # SQLite3 driver
│       ├── mysql.c         # MySQL driver
│       └── postgres.c      # PostgreSQL driver
├── tests/                  # Unit & integration tests
├── examples/               # Usage examples
└── docs/                   # Design documents
```

## Quick Start

### Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
ctest
```

### Minimal Example

```c
#include "corm_pub.h"

typedef struct {
    int id;
    char name[256];
} User;

static corm_field_t user_fields[] = {
    CORM_FIELD(User, id,  CORM_INT, CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC, NULL),
    CORM_FIELD(User, name, CORM_STRING, CORM_FLAG_NOT_NULL, NULL),
};

static corm_model_t user_model = {
    .table_name = "users",
    .struct_size = sizeof(User),
    .fields = user_fields,
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

## API Reference

### Connection

| Function | Description |
|---|---|
| `corm_open(dsn, &db)` | Open with default config |
| `corm_open_with_config(dsn, config, &db)` | Open with custom config |
| `corm_close(db)` | Close connection |
| `corm_ping(db)` | Health check |

### Execution

| Function | Description |
|---|---|
| `corm_exec(db, sql)` | Execute raw SQL |
| `corm_raw(db, sql, &result)` | Execute query, get results |

### Transactions

| Function | Description |
|---|---|
| `corm_begin(db)` | Begin transaction |
| `corm_commit(db)` | Commit transaction |
| `corm_rollback(db)` | Rollback transaction |

### Migration

| Function | Description |
|---|---|
| `corm_auto_migrate(db, models[], count)` | Auto-create tables |

### Query Builder

| Function | Description |
|---|---|
| `corm_query_new(db, model)` | Create new query |
| `corm_query_free(q)` | Free query |
| `corm_query_select(q, cols)` | SELECT columns |
| `corm_query_where(q, cond)` | WHERE condition |
| `corm_query_or_where(q, cond)` | OR WHERE condition |
| `corm_query_order(q, expr)` | ORDER BY |
| `corm_query_group(q, expr)` | GROUP BY |
| `corm_query_having(q, cond)` | HAVING condition |
| `corm_query_limit(q, n)` | LIMIT |
| `corm_query_offset(q, n)` | OFFSET |
| `corm_query_bind(q, val)` | Bind parameter |
| `corm_query_set(q, field, val)` | SET field = ? |
| `corm_find(q, &result)` | Execute SELECT |
| `corm_first(q, record)` | Fetch first row into struct |
| `corm_create(q, record, &id)` | INSERT |
| `corm_update(q, &affected)` | UPDATE |
| `corm_delete(q, &affected)` | DELETE |

### High-Level Convenience

| Function | Description |
|---|---|
| `corm_find_all(db, model, where, records, &count)` | Find all rows |
| `corm_create_one(db, model, record, &id)` | Insert one record |

## Configuration

```c
typedef struct {
    int max_open_conns;        // Max connections (0 = unlimited)
    int max_idle_conns;        // Max idle connections (default: 2)
    int conn_max_lifetime_ms;  // Connection lifetime (0 = no limit)
    int timeout_ms;            // Connection timeout (default: 30000ms)
    bool verbose_logging;      // Enable debug logging
} corm_config_t;

#define CORM_DEFAULT_CONFIG {0, 2, 0, 30000, false}
```

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

Field flags:

| Flag | Value | Description |
|---|---|---|
| `CORM_FLAG_PRIMARY` | `(1<<0)` | Primary key |
| `CORM_FLAG_AUTOINC` | `(1<<1)` | Auto-increment |
| `CORM_FLAG_NOT_NULL` | `(1<<2)` | NOT NULL constraint |
| `CORM_FLAG_UNIQUE` | `(1<<3)` | UNIQUE constraint |

Supported types:

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

## Backend Registration

Backends auto-register via `__attribute__((constructor))`. For static linking without constructors:

```c
// Call early in main()
corm_init();  // Registers all available backends
// Or individually:
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
// ... iterate res ...
corm_result_release(res);
corm_query_free(q);
```

## Testing

```bash
cd build
ctest --output-on-failure
```

Four test suites:

| Suite | Tests | Coverage |
|---|---|---|
| `test_core` | 5 | Connection lifecycle, transactions |
| `test_model` | 13 | Model registry, field mapping |
| `test_query` | 8 | Query builder, SQL generation |
| `test_sqlite` | 32 | SQLite backend, type round-trips |
