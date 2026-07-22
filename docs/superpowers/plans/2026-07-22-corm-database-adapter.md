# CORM — C Database Adapter Layer Implementation Plan

> **For agentic workers:** Implement steps in order. Each step produces compilable, testable code.

**Goal:** Build a generic database adapter layer in C (GORM-inspired) supporting SQLite, MySQL, and PostgreSQL with a chainable query builder, model registration, auto-migration, and connection pooling.

**Architecture:** Backend-agnostic vtable dispatch. Models registered via descriptor structs (C has no reflection). Query builder uses chainable API returning stateful query objects. SQL generation via dialect module.

**Tech Stack:** C99, CMake, libsqlite3, libmysqlclient, libpq

---

## Files to Create

```
include/corm/types.h       — Core types, enums, error codes
include/corm/config.h      — Configuration struct
include/corm/backend.h     — Backend vtable interface
include/corm/model.h       — Model & field descriptors, registration API
include/corm/result.h      — Result set iteration API
include/corm/query.h       — Query builder chainable API
include/corm/corm.h        — Umbrella header (includes all above)
src/internal/strbuf.h      — Dynamic string builder
src/internal/list.h        — Doubly-linked list
src/internal/hash.h        — Hash table
src/internal/pool.h        — Connection pool
src/builder.c              — SQL string builder
src/dialect.c              — SQL dialect differences
src/model.c                — Model registry
src/result.c               — Result set iteration
src/query.c                — Query builder chain
src/stmt.c                 — Prepared statement cache
src/migration.c            — Auto-migration
src/corm.c                 — Init, config, cleanup, error handling
src/backend/backend.c      — Backend registration & dispatch
src/backend/sqlite3.c      — SQLite backend
src/backend/mysql.c        — MySQL backend
src/backend/postgres.c     — PostgreSQL backend
tests/test_core.c          — Core functionality tests
tests/test_model.c         — Model registration tests
tests/test_query.c         — Query builder tests
tests/test_sqlite.c        — SQLite backend tests
examples/basic.c           — Complete usage example
CMakeLists.txt             — Build system
```

## Implementation Order

1. Internal utilities → 2. Public headers → 3. SQL builder + dialect → 4. Model + result → 5. Query builder → 6. Stmt + migration → 7. Core → 8. Backends → 9. Tests → 10. Example → 11. CMake

---

### Task 1: Internal Utilities

**Files:**
- Create: `src/internal/strbuf.h`
- Create: `src/internal/list.h`
- Create: `src/internal/hash.h`
- Create: `src/internal/pool.h`

Implements dynamic string buffer, doubly-linked list, hash table, and connection pool.

### Task 2: Public Headers

**Files:**
- Create: `include/corm/types.h` — Error codes, field types, value union
- Create: `include/corm/config.h` — Connection configuration
- Create: `include/corm/backend.h` — Backend vtable
- Create: `include/corm/model.h` — Model/field descriptors
- Create: `include/corm/result.h` — Result set
- Create: `include/corm/query.h` — Query builder
- Create: `include/corm/corm.h` — Umbrella header

### Task 3: SQL Builder & Dialect

**Files:**
- Create: `src/builder.c` — Build INSERT/SELECT/UPDATE/DELETE SQL from query state
- Create: `src/dialect.c` — Per-backend SQL dialect (placeholders, quoting, type mapping, LIMIT syntax)

### Task 4: Model & Result

**Files:**
- Create: `src/model.c` — Model registry
- Create: `src/result.c` — Result set iteration

### Task 5: Query Builder

**Files:**
- Create: `src/query.c` — Chainable query builder

### Task 6: Prepared Statement Cache & Migration

**Files:**
- Create: `src/stmt.c` — Prepared statement cache
- Create: `src/migration.c` — Auto-migration

### Task 7: Core

**Files:**
- Create: `src/corm.c` — Core init, config, cleanup, error handling

### Task 8: Backends

**Files:**
- Create: `src/backend/backend.c` — Backend registration & dispatch
- Create: `src/backend/sqlite3.c` — SQLite backend
- Create: `src/backend/mysql.c` — MySQL backend
- Create: `src/backend/postgres.c` — PostgreSQL backend

### Task 9: Tests

**Files:**
- Create: `tests/test_core.c`
- Create: `tests/test_model.c`
- Create: `tests/test_query.c`
- Create: `tests/test_sqlite.c`

### Task 10: Example

**Files:**
- Create: `examples/basic.c`

### Task 11: CMake Build

**Files:**
- Create: `CMakeLists.txt`
