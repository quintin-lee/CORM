#ifndef CORM_STMT_CACHE_H
#define CORM_STMT_CACHE_H

#include "corm/types.h"
#include <stddef.h>

typedef struct corm_stmt_entry {
    char *sql;
    void *stmt;
    struct corm_stmt_entry *prev;
    struct corm_stmt_entry *next;
} corm_stmt_entry_t;

typedef struct {
    size_t capacity;
    size_t size;
    corm_stmt_entry_t *head;
    corm_stmt_entry_t *tail;
} corm_stmt_cache_t;

corm_err_t corm_stmt_cache_create(size_t capacity, corm_stmt_cache_t **out_cache);
void* corm_stmt_cache_get(corm_stmt_cache_t *cache, const char *sql);
corm_err_t corm_stmt_cache_put(corm_stmt_cache_t *cache, const char *sql, void *stmt);
void corm_stmt_cache_destroy(corm_stmt_cache_t *cache);

#endif /* CORM_STMT_CACHE_H */
