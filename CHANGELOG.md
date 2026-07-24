# Changelog

All notable changes to CORM will be documented in this file.

## [Unreleased]

### Added
- Multi-backend support: SQLite3, MySQL, PostgreSQL via vtable dispatch
- Auto-migration: `corm_auto_migrate()` creates tables from model definitions
- Insert hooks: `before_create`, `after_create`, `before_batch_create`, `after_batch_create`
- Update hooks: `before_update`, `after_update`
- Delete hooks: `before_delete`, `after_delete`
- Find hook: `after_find`
- Soft delete: `CORM_FLAG_SOFT_DELETE` — `corm_delete()` turns into UPDATE, `corm_find()` auto-filters deleted rows, `corm_query_unscoped()` bypasses filter
- Connection pool: `corm_pool_create()`, `corm_pool_acquire()`, `corm_pool_release()`, `corm_pool_destroy()` — configurable max/idle conns and lifetime
- Prepared statement cache: LRU cache with TTL and max capacity
- Pluggable logger: `corm_set_logger()` with per-execution timing and log levels
- Savepoint support: `corm_savepoint()`, `corm_rollback_to()`, `corm_release_savepoint()`
- Predicate queries: `corm_query_where_in()`, `corm_query_where_between()`, `corm_query_where_null()`, `corm_query_where_not_null()`
- JOIN support: `corm_query_join()` with arbitrary join types
- Batch insert: `corm_create_batch()` inserts multiple rows in one statement
- Transaction closure helper: `corm_transaction()` executes closure functions inside automatic BEGIN/COMMIT/ROLLBACK blocks
- Model schema validation: `corm_validate_model()` validates table names, identifier formats, struct sizes, and duplicate field names upon registration
- Query DISTINCT API: `corm_query_distinct()` adds `DISTINCT` keyword to generated SELECT SQL
- Query COUNT API: `corm_query_count()` calculates row count using current query filters
- Migration composite primary key: `corm_auto_migrate()` detects multiple `CORM_FLAG_PRIMARY` fields and generates table-level `PRIMARY KEY (col1, col2)` constraints
- Connection pool concurrency: optimized lock granularity by executing network/disk I/O (`corm_ping`, connection creation, and retry sleeps) outside the pool mutex lock
- Statement cache thread safety: added `pthread_mutex_t` lock protection to `corm_stmt_cache_t`
- SQL security validation: added `corm_is_valid_identifier()` check to Query Builder field APIs (`where_null`, `where_not_null`, `where_in`, `where_between`, `set`, `preload`) to prevent SQL injection vulnerabilities
- Migration dynamic buffers: replaced static 512-byte SQL buffers in `corm_auto_migrate()` with dynamic `corm_strbuf_t` instances
- Relation Preload: updated `corm_query_preload()` to store relation table targets on `corm_query_t`
- CMake export: `cormConfig.cmake` and `pkg-config corm.pc` for downstream integration
- CI pipeline: GitHub Actions with GCC/Clang, Debug/Release, ASAN builds
- AddressSanitizer build option: `CORM_ENABLE_ASAN=ON`
- `.clang-format` config: explicit LLVM-based formatting standard checked in CI
- Convenience functions: `corm_find_all()`, `corm_create_one()`
- Field value helpers: `corm_field_get_value()`, `corm_field_set_value()`
- Error introspection: `corm_last_error()`, `corm_err_string()`
- Model/field lookup: `corm_find_model()`, `corm_find_field()`
- Public headers refactored into modular headers under `include/corm/`

### Changed
- **MySQL backend**: Replaced `mysql_query()` with prepared statements (`mysql_stmt_prepare` + `mysql_stmt_bind_param`) — parameterized queries now work correctly instead of ignoring parameter values
- **LIMIT/OFFSET**: All backends now use inline literal integers instead of PostgreSQL `$n` placeholders — avoids parameter binding mismatch and is safe since limits are never user-provided strings
- **corm_internal.h**: Reformatted struct members one per line for readability; fixed `corm_backet_t` typo to `corm_backend_t`
- **corm_pub.h** → modular headers: API split into `corm/types.h`, `corm/model.h`, `corm/query.h`, `corm/result.h`, `corm/config.h`, `corm/backend.h`, `corm/pool.h` under `include/corm/` — include `<corm/corm.h>` for the full API

### Fixed
- **query.c**: Fixed `corm_query_where` copy-paste bug — removed erroneous `q->op = CORM_OP_SELECT` inside where clause body
- **query.c**: Fixed `corm_query_or_where` — removed stale `(void)condition` that ignored the condition parameter
- **query.c**: Fixed variadic args UB in `corm_query_where` — removed broken `va_arg` usage, now requires explicit `corm_query_bind()`
- **query.c**: Fixed SQL injection in `corm_query_set` — field names now properly quoted via dialect-aware identifier quoting
- **sqlite3.c**: Fixed `sqlite_escape()` single-quote escaping bug — both branches checked `src[i] == '\''`, escape char was never inserted
- **postgres.c**: Fixed memory leak in `pg_exec`/`pg_query` parameter loop — each INT/FLOAT/DOUBLE param strdup leaked all but last allocation; replaced with `char **tmp_strs` array
- **postgres.c**: Implemented `pg_affected()` using `PQcmdTuples()` from last result instead of always returning 0
- **builder.c**: Added dialect-aware identifier quoting for all table/field references in generated SQL
- **migration.c**: Table name now quoted via `corm_dialect_quote()`
- **dialect.c**: `corm_dialect_limit_offset()` now returns dialect-specific formats
- **builder.c**: INSERT/UPDATE now use dialect-aware placeholders (`$1,$2` for PostgreSQL)
- **model.c**: Fixed heap corruption in `corm_model_registry_free()` — registry does not own static model/field memory
- **sqlite3.c**: Fixed column type detection after `sqlite3_reset()` — types queried post-reset returned SQLITE_NULL for all columns; fixed by determining types during first row fetch
- **corm_open_with_config()**: Added NULL backend function pointer guard — prevents crashes when backend library unavailable
- **All source files**: Applied clang-format LLVM style to eliminate all formatting violations caught by CI lint job

## [0.1.0] - 2026-07-22

### Initial Release
- Core library: connection management, model registry, query builder
- SQLite3 backend with full type support
- 4 test suites: core (5), model (13), query (8), sqlite (32) = 58 tests total
- Basic usage example
