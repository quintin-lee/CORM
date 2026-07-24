# Pool & Batch Performance Optimization Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Production-grade connection pool (idle cap + connection max lifetime) and 5-10x batch INSERT performance via multi-row VALUES.

**Architecture:** Two independent subsystems — pool.c gets config enforcement; query.c gets multi-row INSERT SQL generation. Both are additive, no existing API breaks.

**Tech Stack:** C99, pthreads, sqlite3

---

## Chunk 1: Pool — `max_idle_conns` + `conn_max_lifetime_ms`

### Current state
- `pool.c::corm_pool_release()` unconditionally adds connection to idle queue with no cap or age check
- `config.max_idle_conns` exists but never read
- `config.conn_max_lifetime_ms` exists but never read
- Pool has no `idle_count` field, nodes have no creation timestamp
- `current_open` increments on open but never decrements on idle-close

### Design

**What changes:**
1. `corm_t` struct (`corm_internal.h`): add `uint64_t created_at_ms` — set via `clock_gettime` in `corm_open_with_config`
2. Pool struct (`pool.h`): add `int idle_count` — tracks how many connections are in idle queue
3. `corm_pool_release`: check `max_idle_conns` cap before enqueueing; if at cap, close connection instead
4. `corm_pool_acquire`: check `conn_max_lifetime_ms` on idle connection before returning; if expired, close + try next idle or create new
5. `corm_pool_destroy`: drain idle_count tracking

### Files

- **Modify:** `src/internal/corm_internal.h` — add `created_at_ms` field
- **Modify:** `src/internal/pool.h` — add `idle_count` field
- **Modify:** `src/corm.c` — set `created_at_ms` in `corm_open_with_config`
- **Modify:** `src/internal/pool.c` — implement cap and lifetime checks
- **Modify:** `tests/test_pool.c` — add new test cases

### Implementation

#### Step 1: Add `created_at_ms` to corm_t

**File:** `src/internal/corm_internal.h`

After `int rows_affected_val;` add:
```c
uint64_t created_at_ms; /**< CLOCK_MONOTONIC time at corm_open */
```

#### Step 2: Set created_at_ms in corm_open_with_config

**File:** `src/corm.c` line ~42–95

In `corm_open_with_config`, after `db->err_sql[0] = '\0';` (around line 52), add:
```c
struct timespec now;
clock_gettime(CLOCK_MONOTONIC, &now);
db->created_at_ms = (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
```

This uses the same CLOCK_MONOTONIC already used by `corm_exec`/`corm_raw`.

#### Step 3: Add idle_count to pool struct

**File:** `src/internal/pool.h`

After `int current_open;` add:
```c
int idle_count;
```

Initialize to 0 in `corm_pool_create` (already using calloc, so it's 0 by default).

#### Step 4: Enforce max_idle_conns in corm_pool_release

**File:** `src/internal/pool.c` — `corm_pool_release` function (line 98)

Current logic:
```c
corm_pool_node_t *node = malloc(sizeof(corm_pool_node_t));
node->db = db;
node->next = pool->idle_head;
pool->idle_head = node;
pthread_cond_signal(&pool->cond);
```

New logic:
```c
/* Enforce max_idle_conns: close excess connections instead of idling */
if (pool->config.max_idle_conns > 0 &&
    pool->idle_count >= pool->config.max_idle_conns) {
    pthread_mutex_unlock(&pool->lock);
    corm_close(db);
    return CORM_OK;  /* Not an error — we just closed it */
}

corm_pool_node_t *node = malloc(sizeof(corm_pool_node_t));
if (!node) {
    pthread_mutex_unlock(&pool->lock);
    return CORM_ERR_NOMEM;
}
node->db = db;
node->next = pool->idle_head;
pool->idle_head = node;
pool->idle_count++;
pthread_cond_signal(&pool->cond);
```

#### Step 5: Enforce conn_max_lifetime_ms in corm_pool_acquire

**File:** `src/internal/pool.c` — `corm_pool_acquire` function, idle-return path (line 50–78)

After popping node from idle queue and before returning it:

```c
corm_pool_node_t *node = pool->idle_head;
pool->idle_head = node->next;
pool->idle_count--;  // <-- new
corm_t *db = node->db;
free(node);

// Check connection max lifetime
if (pool->config.conn_max_lifetime_ms > 0) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t now_ms = (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
    if (now_ms - db->created_at_ms > (uint64_t)pool->config.conn_max_lifetime_ms) {
        corm_close(db);
        // Fall through to creating a new connection instead of returning this one
        goto create_new;
    }
}

// Ping check on idle connection (existing code)...
```

Label `create_new:` would be placed before the existing new-connection creation code (line 80), with `*out_db = db;` between and return.

Actually, a cleaner approach: wrap the new-connection code in a helper or just add the lifetime expiry check before the existing ping check. When lifetime expired, close the old connection and `goto` the new-connection section. Let me structure it as:

```c
if (pool->idle_head) {
    corm_pool_node_t *node = pool->idle_head;
    pool->idle_head = node->next;
    pool->idle_count--;  // TRACK
    corm_t *db = node->db;
    free(node);

    // Connection max lifetime check
    if (pool->config.conn_max_lifetime_ms > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t age_ms = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
        age_ms -= db->created_at_ms;
        if (age_ms > (uint64_t)pool->config.conn_max_lifetime_ms) {
            corm_close(db);
            goto open_new;  // bypass ping, go straight to creating a fresh connection
        }
    }

    // Ping check (existing)
    if (corm_ping(db) != CORM_OK) {
        corm_close(db);
        // same retry loop as existing code
        ... 
    }

    *out_db = db;
    pthread_mutex_unlock(&pool->lock);
    return CORM_OK;
}

open_new:
// existing new-connection code (unlock before open to avoid holding lock during I/O)
```

Wait, the existing code unlocks at return points. Let me look at the structure more carefully.

Actually, the current code at line 50-95 does:

```
if (idle_head):
  pop node → db
  if ping fails:
    close db
    retry open (with lock still held)
    if open fails: post_mutex_unlock + return
  *out_db = db; mutex_unlock; return OK

// No idle available
open new connection:
  retry open...
  if ok: pool->current_open++; *out_db = db
  mutex_unlock; return
```

The `goto open_new` would skip the ping check and go directly to the "open new connection" section. But after opening a new connection, it needs to do pool->current_open++ etc. So the cleanest change is:

```c
if (pool->idle_head) {
    ...
    // After pop:
    pool->idle_count--;
    
    // Lifetime check
    if (lifetime_expired) {
        corm_close(db);
        // Don't return, fall through to open_new below
    } else {
        // Ping and return (existing logic)
        ...
        *out_db = db;
        pthread_mutex_unlock(&pool->lock);
        return CORM_OK;
    }
}

// open_new: existing new-connection code
```

This naturally falls through to the new-connection code if lifetime expired. The existing code already handles the "no idle available" case at line 80+.

#### Step 6: Update corm_pool_destroy to manage idle_count

**File:** `src/internal/pool.c` — `corm_pool_destroy`

In the loop that drains idle connections:
```c
while (curr) {
    corm_pool_node_t *next = curr->next;
    corm_close(curr->db);
    free(curr);
    curr = next;
    pool->idle_count--;  // TRACK
}
```

After the loop, `idle_count` will be 0.

#### Step 7: Tests

**File:** `tests/test_pool.c`

Add two test functions:

```c
static void test_pool_idle_limit(void) {
    corm_pool_t *pool = NULL;
    corm_config_t cfg = {.max_open_conns = 10, .max_idle_conns = 2,
                         .timeout_ms = 1000};
    corm_err_t err = corm_pool_create("sqlite3://:memory:", cfg, &pool);
    assert(err == CORM_OK);

    corm_t *conns[5];
    for (int i = 0; i < 5; i++) {
        err = corm_pool_acquire(pool, &conns[i]);
        assert(err == CORM_OK);
    }
    // Release them all — pool should only keep max_idle_conns=2
    for (int i = 0; i < 5; i++)
        corm_pool_release(pool, conns[i]);

    // Verify: only 2 should be alive in idle (others were closed)
    // We can verify by acquiring: should succeed without creating new
    corm_t *c1, *c2, *c3;
    assert(corm_pool_acquire(pool, &c1) == CORM_OK);  // from idle
    assert(corm_pool_acquire(pool, &c2) == CORM_OK);  // from idle
    assert(corm_pool_acquire(pool, &c3) == CORM_OK);  // must create new
    
    corm_pool_release(pool, c1);
    corm_pool_release(pool, c2);
    corm_pool_release(pool, c3);
    corm_pool_destroy(pool);
    printf("test_pool_idle_limit PASSED\n");
}

static void test_pool_conn_max_lifetime(void) {
    corm_pool_t *pool = NULL;
    corm_config_t cfg = {.max_open_conns = 5, .max_idle_conns = 2,
                         .conn_max_lifetime_ms = 50, .timeout_ms = 1000};
    corm_pool_create("sqlite3://:memory:", cfg, &pool);

    corm_t *db = NULL;
    corm_pool_acquire(pool, &db);
    corm_pool_release(pool, db);

    // Wait for lifetime to expire
    usleep(60000);  // 60ms > 50ms

    // Acquire again — should get a NEW connection (old expired)
    corm_t *new_db = NULL;
    corm_pool_acquire(pool, &new_db);
    assert(new_db != db);  // different pointer = different connection
    
    corm_pool_release(pool, new_db);
    corm_pool_destroy(pool);
    printf("test_pool_conn_max_lifetime PASSED\n");
}
```

**Build:** CORM_BUILD_TESTS must add `-lrt` for clock_gettime if not already linked (usually in libc on Linux). Actually `pool.c` already uses clock_gettime so this should be fine.

**Run:** `cmake --build build && ctest --test-dir build -R test_pool -V`

---

## Chunk 2: INSERT batch multi-row VALUES

### Current state
- `corm_create_batch()` loops per-record, calling `corm_create_one()` N times in a transaction
- Each iteration allocates a query, builds SQL, binds params, execs, frees — O(N) overhead
- For 1000 records: 1000 SQL prepares, 1000 exec calls (even in a transaction)

### Design

Replace the inner per-record loop with a single multi-row INSERT per batch:
```
INSERT INTO t (c1, c2) VALUES (?, ?), (?, ?), ..., (?, ?)
```

One SQL string per batch (up to `batch_size` rows), one `exec` call, one backend round-trip. Still wrapped in transaction-per-batch.

**Key changes:**
1. Add `build_multirow_insert_sql()` static function in `query.c` — generates multi-row VALUES SQL with dialect-aware placeholders (`?` for SQLite/MySQL, `$N` for PG)
2. Add `corm_exec_multirow_insert()` — builds SQL, binds params from all records, calls `backend->exec`
3. Refactor `corm_create_batch()` to use the new multi-row path instead of per-record loop
4. No API changes — `corm_create_batch` signature unchanged

### Files

- **Modify:** `src/query.c` — add two static functions; refactor `corm_create_batch`
- **Test:** `tests/test_corm_api.c` — add performance/verify test for multi-row batch

### Implementation

#### Step 1: Add `build_multirow_insert_sql()` in query.c

```c
/* Build multi-row INSERT VALUES clause: (?,?,?), (?,?,?), ... */
static void build_multirow_insert_sql(corm_strbuf_t *sql, corm_model_t *model,
                                       corm_backend_type_t bt, int batch_size) {
    corm_strbuf_append(sql, "INSERT INTO ");
    /* Need qident() from builder.c — either make it non-static or inline quoting */
    // Inline quident:
    const char *lq = corm_dialect_quote(bt, model->table_name);
    corm_strbuf_append(sql, lq);
    corm_strbuf_append(sql, model->table_name);
    corm_strbuf_append(sql, lq);
    corm_strbuf_append(sql, " (");

    int col_count = 0;
    for (int i = 0; i < model->field_count; i++) {
        if (model->fields[i].flags & CORM_FLAG_AUTOINC)
            continue;
        if (col_count > 0)
            corm_strbuf_append(sql, ", ");
        const char *fq = corm_dialect_quote(bt, model->fields[i].name);
        corm_strbuf_append(sql, fq);
        corm_strbuf_append(sql, model->fields[i].name);
        corm_strbuf_append(sql, fq);
        col_count++;
    }
    corm_strbuf_append(sql, ") VALUES ");

    char ph_buf[16];
    int pi = 0;
    for (int b = 0; b < batch_size; b++) {
        if (b > 0)
            corm_strbuf_append(sql, ", ");
        corm_strbuf_append(sql, "(");
        for (int i = 0, j = 0; i < model->field_count; i++) {
            if (model->fields[i].flags & CORM_FLAG_AUTOINC)
                continue;
            if (j > 0)
                corm_strbuf_append(sql, ", ");
            corm_dialect_placeholder_str(bt, pi++, ph_buf, sizeof(ph_buf));
            corm_strbuf_append(sql, ph_buf);
            j++;
        }
        corm_strbuf_append(sql, ")");
    }
}
```

This duplicates small parts of builder.c (quoted identifier, column list) but keeps the implementation self-contained in query.c. The duplication is acceptable — it's ~10 lines of trivial identifier quoting.

#### Step 2: Add `corm_exec_multirow_insert()` in query.c

```c
static corm_err_t corm_exec_multirow_insert(corm_t *db, corm_model_t *model,
                                             void *records, int batch_size) {
    corm_query_t *q = corm_query_new(db, model);
    if (!q)
        return CORM_ERR_NOMEM;

    corm_strbuf_t sql;
    corm_strbuf_init(&sql);
    build_multirow_insert_sql(&sql, model, db->backend->type, batch_size);

    /* Bind params from all records */
    uint8_t *bytes = (uint8_t *)records;
    for (int b = 0; b < batch_size; b++) {
        void *rec = bytes + b * model->struct_size;
        for (int i = 0; i < model->field_count; i++) {
            corm_field_t *f = &model->fields[i];
            if (f->flags & CORM_FLAG_AUTOINC)
                continue;
            corm_value_t val = corm_field_get_value(rec, f);
            corm_query_bind(q, val);
        }
    }

    corm_err_t err = db->backend->exec(db, corm_strbuf_cstr(&sql),
                                        q->params, q->param_count);
    corm_strbuf_free(&sql);
    corm_query_free(q);
    return err;
}
```

#### Step 3: Refactor `corm_create_batch()`

**Old (current) — in the inner batch loop:**
```c
corm_begin(db);
for (int j = 0; j < current_batch; j++) {
    void *rec = bytes + (i + j) * model->struct_size;
    int64_t id = 0;
    corm_err_t err = corm_create_one(db, model, rec, &id);
    if (err != CORM_OK) {
        corm_rollback(db);
        ...
    }
    total_inserted++;
}
corm_commit(db);
```

**New:**
```c
corm_begin(db);
corm_err_t err = corm_exec_multirow_insert(db, model,
                                            bytes + i * model->struct_size,
                                            current_batch);
if (err != CORM_OK) {
    corm_rollback(db);
    if (inserted_count)
        *inserted_count = total_inserted;
    return err;
}
total_inserted += current_batch;
corm_commit(db);
```

No more per-record hook calls (`before_create`/`after_create`). This is the documented trade-off: batch operations skip per-record hooks for performance. Users who need hooks should use individual `corm_create_one` calls.

#### Step 4: Update batch comment/doc

**File:** `include/corm/corm.h` — update the batch docstring to note:
- Multi-row VALUES optimization
- Hooks are NOT called per record in batch mode

#### Step 5: Tests

**File:** `tests/test_corm_api.c`

Add:
```c
static void test_create_batch_multirow(void) {
    corm_t *db = NULL;
    corm_err_t err = corm_open("sqlite3://:memory:", &db);
    if (err != CORM_OK) {
        printf("SKIP (no sqlite3 backend)\n");
        return;
    }

    corm_register_model(db, &person_model);
    corm_model_t *models[] = {&person_model};
    corm_auto_migrate(db, models, 1);

    PersonRec people[100];
    for (int i = 0; i < 100; i++) {
        snprintf(people[i].name, sizeof(people[i].name), "Person_%d", i);
        people[i].age = i;
    }

    int inserted = 0;
    err = corm_create_batch(db, &person_model, people, 100, 25, &inserted);
    assert(err == CORM_OK);
    assert(inserted == 100);

    // Verify via count
    int cnt = 0;
    corm_count(db, &person_model, NULL, &cnt);
    assert(cnt == 100);

    // Verify names (spot-check first, middle, last)
    PersonRec found;
    corm_find_one(db, &person_model, "id = 1", &found);
    assert(strcmp(found.name, "Person_0") == 0);
    corm_find_one(db, &person_model, "id = 50", &found);
    assert(strcmp(found.name, "Person_49") == 0);
    corm_find_one(db, &person_model, "id = 100", &found);
    assert(strcmp(found.name, "Person_99") == 0);

    corm_close(db);
    printf("test_create_batch_multirow PASSED\n");
}
```

Also verify that `corm_create_batch` with `count=1` still works (edge case — single-row batch should produce valid `VALUES (?, ?)` without trailing comma).

### Performance expectation

| Batch size | Individual INSERTs (current) | Multi-row VALUES (new) | Improvement |
|-----------|------------------------------|----------------------|-------------|
| 100 rows  | 100 × SQL prepare + exec     | 1 × SQL + exec       | ~5-10x      |
| 1000 rows | 1000 × SQL prepare + exec    | ~10 × SQL + exec (batch_size=100) | ~5-10x |

### Safety notes

- The multi-row VALUES SQL is built with `corm_strbuf` which auto-grows — no SQL injection risk since field names are `corm_field_t` constants from model definition
- Placeholder generation correctly handles PG `$N` with sequential global indexing
- `CLOCK_MONOTONIC` (already used in pool and exec) is used consistently

---

## Verification

After both chunks:

```bash
cmake -S . -B build \
  -DCORM_BUILD_TESTS=ON -DCORM_SQLITE3=ON \
  -DCORM_MYSQL=OFF -DCORM_POSTGRES=OFF \
  -DCMAKE_C_FLAGS="-Wall -Wextra -Wunused -Werror"
cmake --build build
ctest --test-dir build --output-on-failure
```

All 15+ existing tests must pass, plus new tests. Zero warnings.
