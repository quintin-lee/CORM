# Contributing to CORM

Thank you for your interest in contributing to CORM! This document provides guidelines and information for contributors.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [How Can I Contribute?](#how-can-i-contribute)
- [Development Setup](#development-setup)
- [Submitting Changes](#submitting-changes)
- [Coding Standards](#coding-standards)
- [Testing](#testing)
- [Adding a New Backend](#adding-a-new-backend)
- [Documentation](#documentation)
- [Report Bugs / Request Features](#report-bugs--request-features)

## Code of Conduct

Be respectful, inclusive, and constructive. Harassment, trolling, or personal attacks will not be tolerated.

## How Can I Contribute?

### Reporting Bugs
1. **Search existing issues** first — someone may have reported it.
2. Include a **minimal reproducible example** (a small C program that triggers the bug).
3. State your **environment**: OS, compiler version, CORM version, backend(s) used.
4. Include **full error output** (compiler errors, runtime crashes, ASAN logs).

### Suggesting Features
1. Search existing issues to avoid duplicates.
2. Describe the **use case** — what problem does this solve?
3. If possible, sketch a **sample API** showing how you'd use it.
4. Note the **priority** for your project — this helps us triage.

### Writing Code
- Fork the repo, create a feature branch (`feat/my-feature`), make changes, submit a PR.
- Small, focused PRs are preferred over large monolithic ones.
- Every PR should include tests if it adds or modifies behavior.

### Writing Documentation
- Improvements to README, ARCHITECTURE, CHANGELOG, or inline comments are welcome.
- New users finding unclear docs? That's a great first contribution.

## Development Setup

### Prerequisites

| Tool | Minimum Version | Purpose |
|---|---|---|
| GCC / Clang | C99 compliant | Compiler |
| CMake | 3.10+ | Build system |
| SQLite3 dev | Any | Required for tests |
| MySQL dev (optional) | Any | MySQL backend + tests |
| PostgreSQL dev (optional) | Any | PostgreSQL backend + tests |

### Build & Test

```bash
# Configure (finds SQLite3 by default)
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build
make

# Run all tests
ctest --output-on-failure

# Build with specific backends
cmake .. -DCORM_WITH_SQLITE3=ON -DCORM_WITH_MYSQL=OFF -DCORM_WITH_POSTGRES=ON
```

### Debugging

```bash
# With AddressSanitizer (catches memory bugs)
cmake .. -DCMAKE_C_FLAGS="-fsanitize=address -g"
make
ctest --output-on-failure

# With GDB
gdb --args tests/test_core
(gdb) run
(gdb) bt          # on crash
```

## Submitting Changes

### Git Workflow

1. **Create a branch** from `main`: `git checkout -b feat/my-change`
2. **Make changes** with clear, atomic commits.
3. **Run tests** locally before submitting: `ctest`
4. **Push and open a Pull Request**.

### Commit Message Format

We use [Conventional Commits](https://www.conventionalcommits.org/):

```
type(scope): description

<optional body>
```

**Types:** `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `ci`, `perf`

**Examples:**
```
fix(sqlite3): handle NULL in sqlite_escape()
test(query): add param binding for WHERE clause
docs: update ARCHITECTURE.md with result set section
chore: bump version to 0.2.0
```

### Pull Request Checklist

- [ ] Tests pass (`ctest`)
- [ ] Code follows project conventions (see below)
- [ ] New features include documentation
- [ ] New code includes tests
- [ ] Commit messages follow Conventional Commits
- [ ] No unrelated files changed

## Coding Standards

### Style

| Rule | Convention |
|---|---|
| Indentation | 4 spaces |
| Line length | 120 chars max |
| Brace style | K&R (opening brace on same line) |
| Naming | `snake_case` for functions/vars, `UPPER_CASE` for macros |
| Struct types | `name_t` suffix (e.g., `corm_query_t`) |
| Enums | Same as struct types: `corm_err_t` |
| Constants | `CORM_` prefix (e.g., `CORM_OK`, `CORM_ERR_NOMEM`) |

### C99 Compliance

- No C++ comments (`//`) — use `/* */`
- No mixed declarations and code (declare variables at top of block)
- No variable-length arrays
- Use `stdbool.h` for `bool` type
- Use `stdint.h` for fixed-width integers (`int32_t`, `int64_t`)

### Memory Safety

- **Always check malloc/calloc/realloc return values** — return `CORM_ERR_NOMEM` on failure.
- **Free in reverse order of allocation** — track ownership clearly.
- **Initialize all fields** — zero-initialize structs with `{0}` or explicit assignment.
- **Avoid double-free** — set pointers to NULL after free.
- **Use ASAN in CI** — catch leaks and use-after-free early.

### Error Handling

- Return `corm_err_t` from all functions that can fail.
- Set `db->last_err` and `db->err_msg` on error paths.
- Provide `corm_last_error(db, buf, bufsz)` for callers to retrieve the error message.
- Never leak on error — clean up allocated resources before returning error codes.

### Example Code Style

```c
/* Good: proper error handling, clean naming, K&R braces */
static corm_err_t allocate_rows(corm_result_t *r, int row_count) {
    if (!r || row_count <= 0) return CORM_ERR_NULL;

    r->rows = calloc((size_t)row_count, sizeof(corm_value_t *));
    if (!r->rows) return CORM_ERR_NOMEM;

    for (int i = 0; i < row_count; i++) {
        r->rows[i] = calloc(r->column_count, sizeof(corm_value_t));
        if (!r->rows[i]) {
            /* Clean up already-allocated rows */
            for (int j = 0; j < i; j++) {
                free(r->rows[j]);
            }
            free(r->rows);
            r->rows = NULL;
            return CORM_ERR_NOMEM;
        }
    }
    return CORM_OK;
}
```

## Testing

### Test Organization

| File | Tests | Purpose |
|---|---|---|
| `test_core.c` | Connection lifecycle, transactions, error handling | Core API |
| `test_model.c` | Model registry, field mapping, hooks | Model system |
| `test_query.c` | Query builder, SQL generation, placeholders | Query system |
| `test_sqlite.c` | Type round-trips, NULL handling, blobs | SQLite backend |

### Writing Tests

Each test file follows this pattern:

```c
static void test_<feature>(void) {
    corm_t *db;
    corm_err_t err = corm_open("sqlite3://:memory:", &db);
    CU_ASSERT_EQUAL(err, CORM_OK);

    /* ... test logic ... */

    corm_close(db);
}

int main(void) {
    pSuite suite = CU_add_suite("<SuiteName>", init_suite, clean_suite);
    CU_add_test(suite, "test_feature", test_feature);
    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    return CU_get_number_of_tests_failed() > 0 ? 1 : 0;
}
```

### Test Coverage Goals

- **Core**: connection open/close, transaction rollback, error propagation
- **Model**: all field types, hook callbacks, registry collisions
- **Query**: every query builder method, SQL injection prevention, placeholder substitution
- **Backend**: NULL values, BLOB round-trip, large integers, boolean mapping

## Adding a New Backend

### Step-by-Step

1. **Create `src/backend/<name>.c`** implementing `corm_backend_t`:

```c
#include "corm_pub.h"
#include "../internal/corm_internal.h"

#ifdef CORM_HAVE_<NAME>

/* Implement each vtable function */
static corm_err_t <name>_open(corm_t *db, const char *dsn) {
    /* Allocate connection handle, store in db->conn */
    return CORM_OK;
}

/* ... open, close, ping, exec, query, begin, commit, rollback,
      escape_string, last_insert_id, rows_affected ... */

static corm_backend_t <name>_backend = {
    .name = "<name>",
    .type = CORM_BACKEND_<NAME>,
    .open = <name>_open,
    /* ... fill all function pointers ... */
};

__attribute__((constructor))
static void <name>_register(void) {
    corm_register_backend(CORM_BACKEND_<NAME>, &<name>_backend);
}

corm_err_t corm_register_<name>_backend(void) {
    return corm_register_backend(CORM_BACKEND_<NAME>, &<name>_backend);
}

#else
/* Stub with NULL function pointers */
#endif
```

2. **Update `CMakeLists.txt`**:
   - Add `find_package` or `find_library` for the dependency
   - Add `-DCORM_HAVE_<NAME>` definition
   - Add source file to `CORM_SOURCES`
   - Link library to `corm` target

3. **Update `src/corm_pub.h`**:
   - Add `CORM_BACKEND_<NAME>` to `corm_backend_type_t` enum

4. **Update `src/dialect.c`**:
   - Add case in `corm_dialect_quote()` (identifier quote character)
   - Add case in `corm_dialect_placeholder()` (parameter marker)
   - Add case in `corm_dialect_autoinc()` (auto-increment syntax)
   - Add case in `corm_dialect_limit_offset()` (pagination syntax)
   - Add case in `corm_dialect_type_name()` (type mapping)

5. **Write tests** in `tests/test_<name>.c` covering basic CRUD operations.

6. **Update documentation**:
   - Add backend to README.md features table
   - Add backend to ARCHITECTURE.md
   - Update CHANGELOG.md

## Documentation

### README.md
- User-facing: quick start, features, build instructions, API reference
- Keep examples minimal and runnable
- Update when new features are added

### ARCHITECTURE.md
- Internal design for contributors
- Covers data flow, component responsibilities, design decisions
- Keep in sync with code changes

### CHANGELOG.md
- Document all notable changes per release
- Categories: Added, Changed, Fixed, Removed, Deprecated, Security
- Use imperative mood: "fix()", "add()", "update()"

## Report Bugs / Request Features

Use the GitHub issue tracker:
- **Bug reports**: label `bug`, include reproduction steps
- **Feature requests**: label `enhancement`, include use case
- **Questions**: label `question` or `discussion`

## Getting Help

- Read `ARCHITECTURE.md` to understand internal design
- Check `tests/` for usage examples
- Ask questions via GitHub issues — we're happy to help!
