#include "stmt_cache.h"
#include <stdlib.h>
#include <string.h>

corm_err_t corm_stmt_cache_create(size_t capacity,
                                  corm_stmt_cache_t **out_cache) {
  if (!out_cache || capacity == 0)
    return CORM_ERR_NULL;
  corm_stmt_cache_t *cache = calloc(1, sizeof(corm_stmt_cache_t));
  if (!cache)
    return CORM_ERR_NOMEM;

  cache->capacity = capacity;
  *out_cache = cache;
  return CORM_OK;
}

void *corm_stmt_cache_get(corm_stmt_cache_t *cache, const char *sql) {
  if (!cache || !sql)
    return NULL;
  corm_stmt_entry_t *curr = cache->head;
  while (curr) {
    if (strcmp(curr->sql, sql) == 0) {
      // Move to head (LRU)
      if (curr != cache->head) {
        if (curr->prev)
          curr->prev->next = curr->next;
        if (curr->next)
          curr->next->prev = curr->prev;
        if (curr == cache->tail)
          cache->tail = curr->prev;

        curr->next = cache->head;
        curr->prev = NULL;
        if (cache->head)
          cache->head->prev = curr;
        cache->head = curr;
      }
      return curr->stmt;
    }
    curr = curr->next;
  }
  return NULL;
}

corm_err_t corm_stmt_cache_put(corm_stmt_cache_t *cache, const char *sql,
                               void *stmt) {
  if (!cache || !sql || !stmt)
    return CORM_ERR_NULL;

  // Check if exists
  void *existing = corm_stmt_cache_get(cache, sql);
  if (existing)
    return CORM_OK;

  // Evict tail if full
  if (cache->size >= cache->capacity && cache->tail) {
    corm_stmt_entry_t *evict = cache->tail;
    if (evict->prev)
      evict->prev->next = NULL;
    cache->tail = evict->prev;
    if (cache->head == evict)
      cache->head = NULL;

    free(evict->sql);
    free(evict);
    cache->size--;
  }

  corm_stmt_entry_t *entry = calloc(1, sizeof(corm_stmt_entry_t));
  if (!entry)
    return CORM_ERR_NOMEM;

  entry->sql = strdup(sql);
  entry->stmt = stmt;

  entry->next = cache->head;
  if (cache->head)
    cache->head->prev = entry;
  cache->head = entry;
  if (!cache->tail)
    cache->tail = entry;

  cache->size++;
  return CORM_OK;
}

void corm_stmt_cache_destroy(corm_stmt_cache_t *cache) {
  if (!cache)
    return;
  corm_stmt_entry_t *curr = cache->head;
  while (curr) {
    corm_stmt_entry_t *next = curr->next;
    free(curr->sql);
    free(curr);
    curr = next;
  }
  free(cache);
}
