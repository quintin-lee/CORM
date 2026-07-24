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
  pthread_mutex_init(&cache->lock, NULL);
  *out_cache = cache;
  return CORM_OK;
}

static void *corm_stmt_cache_get_unlocked(corm_stmt_cache_t *cache,
                                          const char *sql) {
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

void *corm_stmt_cache_get(corm_stmt_cache_t *cache, const char *sql) {
  if (!cache || !sql)
    return NULL;
  pthread_mutex_lock(&cache->lock);
  void *res = corm_stmt_cache_get_unlocked(cache, sql);
  pthread_mutex_unlock(&cache->lock);
  return res;
}

corm_err_t corm_stmt_cache_put(corm_stmt_cache_t *cache, const char *sql,
                               void *stmt) {
  if (!cache || !sql || !stmt)
    return CORM_ERR_NULL;

  pthread_mutex_lock(&cache->lock);

  // Check if exists
  void *existing = corm_stmt_cache_get_unlocked(cache, sql);
  if (existing) {
    pthread_mutex_unlock(&cache->lock);
    return CORM_OK;
  }

  // Evict tail if full
  if (cache->size >= cache->capacity && cache->tail) {
    corm_stmt_entry_t *evict = cache->tail;
    if (evict->prev)
      evict->prev->next = NULL;
    cache->tail = evict->prev;
    if (cache->head == evict)
      cache->head = NULL;

    if (cache->entry_destroy_fn && evict->stmt)
      cache->entry_destroy_fn(evict->stmt);
    free(evict->sql);
    free(evict);
    cache->size--;
  }

  corm_stmt_entry_t *entry = calloc(1, sizeof(corm_stmt_entry_t));
  if (!entry) {
    pthread_mutex_unlock(&cache->lock);
    return CORM_ERR_NOMEM;
  }

  entry->sql = strdup(sql);
  if (!entry->sql) {
    free(entry);
    pthread_mutex_unlock(&cache->lock);
    return CORM_ERR_NOMEM;
  }
  entry->stmt = stmt;

  entry->next = cache->head;
  if (cache->head)
    cache->head->prev = entry;
  cache->head = entry;
  if (!cache->tail)
    cache->tail = entry;

  cache->size++;
  pthread_mutex_unlock(&cache->lock);
  return CORM_OK;
}

void corm_stmt_cache_set_destroy_fn(corm_stmt_cache_t *cache,
                                    void (*fn)(void *)) {
  if (!cache)
    return;
  pthread_mutex_lock(&cache->lock);
  cache->entry_destroy_fn = fn;
  pthread_mutex_unlock(&cache->lock);
}

void *corm_stmt_cache_remove(corm_stmt_cache_t *cache, const char *sql) {
  if (!cache || !sql)
    return NULL;
  pthread_mutex_lock(&cache->lock);
  corm_stmt_entry_t *curr = cache->head;
  while (curr) {
    if (strcmp(curr->sql, sql) == 0) {
      void *stmt = curr->stmt;
      /* Unlink */
      if (curr->prev)
        curr->prev->next = curr->next;
      if (curr->next)
        curr->next->prev = curr->prev;
      if (curr == cache->head)
        cache->head = curr->next;
      if (curr == cache->tail)
        cache->tail = curr->prev;
      free(curr->sql);
      free(curr);
      cache->size--;
      pthread_mutex_unlock(&cache->lock);
      return stmt;
    }
    curr = curr->next;
  }
  pthread_mutex_unlock(&cache->lock);
  return NULL;
}

void corm_stmt_cache_destroy(corm_stmt_cache_t *cache) {
  if (!cache)
    return;
  pthread_mutex_lock(&cache->lock);
  corm_stmt_entry_t *curr = cache->head;
  while (curr) {
    corm_stmt_entry_t *next = curr->next;
    if (cache->entry_destroy_fn && curr->stmt)
      cache->entry_destroy_fn(curr->stmt);
    free(curr->sql);
    free(curr);
    curr = next;
  }
  pthread_mutex_unlock(&cache->lock);
  pthread_mutex_destroy(&cache->lock);
  free(cache);
}
