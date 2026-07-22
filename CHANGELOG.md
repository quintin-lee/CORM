# Changelog

All notable changes to CORM will be documented in this file.

## [Unreleased]

### Added
- Multi-backend support: SQLite3, MySQL, PostgreSQL via vtable dispatch
- Auto-migration: `corm_auto_migrate()` creates tables from model definitions
- Query builder: Fluent API with SELECT/INSERT/UPDATE/DELETE operations
- Model hooks: before_create, after_create, before_update, after_update, before_delete, after_delete, after_find callbacks
- Field value helpers: `corm_field_get_value()`, `corm_field_set_value()` for automatic struct↔record mapping
- Result iterator: forward-only cursor with typed accessors (`corm_result_int`, `corm_result_double`, etc.)
- Dialect abstraction: identifier quoting, type names, placeholders per backend
- SQL builder: `corm_build_sql()` generates SQL from query state

### Fixed
- **query.c**: Fixed variadic args UB in `corm_query_where` — removed broken `va_arg` usage, now requires explicit `corm_query_bind()`
- **query.c**: Fixed SQL injection in `corm_query_set` — field names now properly quoted via dialect-aware identifier quoting
- **sqlite3.c**: Fixed `sqlite_escape()` single-quote escaping bug — both branches checked `src[i] == '\''`, escape char was never inserted
- **postgres.c**: Fixed memory leak in `pg_exec`/`pg_query` parameter loop — each INT/FLOAT/DOUBLE param strdup leaked all but last allocation; replaced with `char **tmp_strs` array
- **postgres.c**: Implemented `pg_affected()` using `PQcmdTuples()` from last result instead of always returning 0
- **builder.c**: Added dialect-aware identifier quoting for all table/field references in generated SQL
- **migration.c**: Table name now quoted via `corm_dialect_quote()`
- **dialect.c**: `corm_dialect_limit_offset()` now returns dialect-specific formats (SQLite literal numbers, MySQL `?`, PostgreSQL `$n`)
- **builder.c**: INSERT/UPDATE now use dialect-aware placeholders (`$1,$2` for PostgreSQL)
- **model.c**: Fixed heap corruption in `corm_model_registry_free()` — registry does not own static model/field memory
- **sqlite3.c**: Fixed column type detection after `sqlite3_reset()` — types queried post-reset returned SQLITE_NULL for all columns; fixed by determining types during first row fetch
- **corm_open_with_config()**: Added NULL backend function pointer guard — prevents crashes when backend library unavailable

## [0.1.0] - 2026-07-22

### Initial Release
- Core library: connection management, model registry, query builder
- SQLite3 backend with full type support
- 4 test suites: core (5), model (13), query (8), sqlite (32) = 58 tests total
- Basic usage example
