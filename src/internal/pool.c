#include "pool.h"
#include <stdlib.h>
#include <string.h>

corm_err_t corm_pool_create(const char *dsn, corm_config_t config,
                            corm_pool_t **out_pool) {
  if (!dsn || !out_pool)
    return CORM_ERR_NULL;
  corm_pool_t *pool = calloc(1, sizeof(corm_pool_t));
  if (!pool)
    return CORM_ERR_NOMEM;

  strncpy(pool->dsn, dsn, sizeof(pool->dsn) - 1);
  pool->config = config;
  pthread_mutex_init(&pool->lock, NULL);
  pthread_cond_init(&pool->cond, NULL);

  *out_pool = pool;
  return CORM_OK;
}

#include <time.h>

corm_err_t corm_pool_acquire(corm_pool_t *pool, corm_t **out_db) {
  if (!pool || !out_db)
    return CORM_ERR_NULL;
  pthread_mutex_lock(&pool->lock);

  while (!pool->idle_head && pool->config.max_open_conns > 0 &&
         pool->current_open >= pool->config.max_open_conns) {
    if (pool->config.timeout_ms > 0) {
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_sec += pool->config.timeout_ms / 1000;
      ts.tv_nsec += (pool->config.timeout_ms % 1000) * 1000000;
      if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
      }
      int rc = pthread_cond_timedwait(&pool->cond, &pool->lock, &ts);
      if (rc != 0 && !pool->idle_head) {
        pthread_mutex_unlock(&pool->lock);
        return CORM_ERR_GENERIC;
      }
    } else {
      pthread_cond_wait(&pool->cond, &pool->lock);
    }
  }

  if (pool->idle_head) {
    corm_pool_node_t *node = pool->idle_head;
    pool->idle_head = node->next;
    corm_t *db = node->db;
    free(node);

    // Ping check on idle connection
    if (corm_ping(db) != CORM_OK) {
      corm_close(db);
      corm_err_t err = corm_open_with_config(pool->dsn, pool->config, &db);
      if (err != CORM_OK) {
        pool->current_open--;
        pthread_mutex_unlock(&pool->lock);
        return err;
      }
    }

    *out_db = db;
    pthread_mutex_unlock(&pool->lock);
    return CORM_OK;
  }

  corm_t *db = NULL;
  corm_err_t err = corm_open_with_config(pool->dsn, pool->config, &db);
  if (err == CORM_OK) {
    pool->current_open++;
    *out_db = db;
  }
  pthread_mutex_unlock(&pool->lock);
  return err;
}

corm_err_t corm_pool_release(corm_pool_t *pool, corm_t *db) {
  if (!pool || !db)
    return CORM_ERR_NULL;
  pthread_mutex_lock(&pool->lock);

  corm_pool_node_t *node = malloc(sizeof(corm_pool_node_t));
  if (!node) {
    pthread_mutex_unlock(&pool->lock);
    return CORM_ERR_NOMEM;
  }
  node->db = db;
  node->next = pool->idle_head;
  pool->idle_head = node;

  pthread_cond_signal(&pool->cond);
  pthread_mutex_unlock(&pool->lock);
  return CORM_OK;
}

void corm_pool_destroy(corm_pool_t *pool) {
  if (!pool)
    return;
  pthread_mutex_lock(&pool->lock);
  corm_pool_node_t *curr = pool->idle_head;
  while (curr) {
    corm_pool_node_t *next = curr->next;
    corm_close(curr->db);
    free(curr);
    curr = next;
  }
  pthread_mutex_unlock(&pool->lock);
  pthread_mutex_destroy(&pool->lock);
  pthread_cond_destroy(&pool->cond);
  free(pool);
}
