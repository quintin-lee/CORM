#include <stdlib.h>
#include <string.h>
#include "corm_pub.h"

/* ── Prepared statement cache ──
 * In-memory cache mapping SQL strings to compiled statements.
 * Each backend defines its own statement type.
 */

typedef struct corm_stmt_entry {
    char *sql;
    void *stmt;          /* backend-specific compiled statement */
    int refcount;
    struct corm_stmt_entry *next;
} corm_stmt_entry_t;

typedef struct {
    corm_stmt_entry_t **buckets;
    size_t size;
    int count;
} corm_stmt_cache_t;

corm_err_t corm_stmt_cache_init(corm_stmt_cache_t *cache) {
    cache->size = 64;
    cache->count = 0;
    cache->buckets = (corm_stmt_entry_t **)calloc(cache->size, sizeof(corm_stmt_entry_t *));
    if (!cache->buckets) return CORM_ERR_NOMEM;
    return CORM_OK;
}

static size_t stmt_hash(const char *sql, size_t size) {
    size_t h = 5381;
    while (*sql) h = ((h << 5) + h) + (unsigned char)*sql++;
    return h % size;
}

void *corm_stmt_cache_get(corm_stmt_cache_t *cache, const char *sql) {
    if (!cache->buckets) return NULL;
    size_t idx = stmt_hash(sql, cache->size);
    corm_stmt_entry_t *entry = cache->buckets[idx];
    while (entry) {
        if (strcmp(entry->sql, sql) == 0) {
            entry->refcount++;
            return entry->stmt;
        }
        entry = entry->next;
    }
    return NULL;
}

corm_err_t corm_stmt_cache_put(corm_stmt_cache_t *cache, const char *sql, void *stmt) {
    size_t idx = stmt_hash(sql, cache->size);
    corm_stmt_entry_t *entry = (corm_stmt_entry_t *)malloc(sizeof(corm_stmt_entry_t));
    if (!entry) return CORM_ERR_NOMEM;
    entry->sql = strdup(sql);
    entry->stmt = stmt;
    entry->refcount = 1;
    entry->next = cache->buckets[idx];
    cache->buckets[idx] = entry;
    cache->count++;
    return CORM_OK;
}

void corm_stmt_cache_free_entry(corm_stmt_cache_t *cache, const char *sql,
                                void (*free_fn)(void *)) {
    if (!cache->buckets) return;
    size_t idx = stmt_hash(sql, cache->size);
    corm_stmt_entry_t **pp = &cache->buckets[idx];
    while (*pp) {
        corm_stmt_entry_t *entry = *pp;
        if (strcmp(entry->sql, sql) == 0) {
            entry->refcount--;
            if (entry->refcount <= 0) {
                *pp = entry->next;
                if (free_fn && entry->stmt) free_fn(entry->stmt);
                free(entry->sql);
                free(entry);
                cache->count--;
            }
            return;
        }
        pp = &entry->next;
    }
}

void corm_stmt_cache_free_all(corm_stmt_cache_t *cache, void (*free_fn)(void *)) {
    if (!cache->buckets) return;
    for (size_t i = 0; i < cache->size; i++) {
        corm_stmt_entry_t *entry = cache->buckets[i];
        while (entry) {
            corm_stmt_entry_t *next = entry->next;
            if (free_fn && entry->stmt) free_fn(entry->stmt);
            free(entry->sql);
            free(entry);
            entry = next;
        }
        cache->buckets[i] = NULL;
    }
    cache->count = 0;
}

void corm_stmt_cache_destroy(corm_stmt_cache_t *cache, void (*free_fn)(void *)) {
    corm_stmt_cache_free_all(cache, free_fn);
    free(cache->buckets);
    cache->buckets = NULL;
    cache->size = 0;
}
