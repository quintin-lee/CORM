#ifndef CROM_POOL_H
#define CROM_POOL_H

#include <stddef.h>
#include <stdlib.h>

#include "corm_pub.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct corm_pool_entry {
    void *conn;
    int64_t created_at;
    struct corm_pool_entry *next;
} corm_pool_entry_t;

typedef struct {
    corm_pool_entry_t *head;
    int max_open;
    int max_idle;
    int total_open;
} corm_pool_t;

static inline void corm_pool_init(corm_pool_t *pool, int max_open, int max_idle) {
    pool->head = NULL;
    pool->max_open = max_open > 0 ? max_open : 0;
    pool->max_idle = max_idle > 0 ? max_idle : 2;
    pool->total_open = 0;
}

static inline corm_err_t corm_pool_push(corm_pool_t *pool, void *conn) {
    corm_pool_entry_t *entry = (corm_pool_entry_t *)malloc(sizeof(corm_pool_entry_t));
    if (!entry) return CORM_ERR_NOMEM;
    entry->conn = conn;
    entry->created_at = 0; /* simplified: no time.h dependency required */
    entry->next = pool->head;
    pool->head = entry;
    pool->total_open++;
    return CORM_OK;
}

static inline void *corm_pool_pop(corm_pool_t *pool) {
    if (!pool->head) return NULL;
    corm_pool_entry_t *entry = pool->head;
    void *conn = entry->conn;
    pool->head = entry->next;
    free(entry);
    pool->total_open--;
    return conn;
}

static inline int corm_pool_count(corm_pool_t *pool) {
    int count = 0;
    corm_pool_entry_t *cur = pool->head;
    while (cur) { count++; cur = cur->next; }
    return count;
}

static inline void corm_pool_drain(corm_pool_t *pool, void (*close_fn)(void *conn)) {
    while (pool->head) {
        void *conn = corm_pool_pop(pool);
        if (close_fn && conn) close_fn(conn);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* CROM_POOL_H */
