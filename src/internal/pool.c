#include "pool.h"
#include "corm_internal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Retry sleep with random jitter (50% of base_ns) to avoid thundering herd */
static void corm_retry_sleep(long base_ns) {
  long jitter = (long)(((double)rand() / RAND_MAX) * (base_ns / 2));
  struct timespec ts = {.tv_sec = 0, .tv_nsec = base_ns + jitter};
  nanosleep(&ts, NULL);
}

corm_err_t corm_pool_create(const char *dsn, corm_config_t config,
                            corm_pool_t **out_pool) {
  if (!dsn || !out_pool)
    return CORM_ERR_NULL;
  corm_pool_t *pool = calloc(1, sizeof(corm_pool_t));
  if (!pool)
    return CORM_ERR_NOMEM;

  snprintf(pool->dsn, sizeof(pool->dsn), "%s", dsn);
  pool->config = config;
  pthread_mutex_init(&pool->lock, NULL);
  pthread_cond_init(&pool->cond, NULL);

  *out_pool = pool;
  return CORM_OK;
}

corm_err_t corm_pool_acquire(corm_pool_t *pool, corm_t **out_db) {
  if (!pool || !out_db)
    return CORM_ERR_NULL;

retry_acquire:
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
    pool->idle_count--;
    corm_t *db = node->db;
    free(node);

    /* Check conn_max_lifetime_ms before reusing */
    bool expired = false;
    if (pool->config.conn_max_lifetime_ms > 0) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      int64_t now_ms =
          (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
      if (now_ms - db->created_at_ms >= pool->config.conn_max_lifetime_ms) {
        expired = true;
      }
    }

    if (expired) {
      corm_close(db);
      pool->current_open--;
      pthread_cond_signal(&pool->cond);
      pthread_mutex_unlock(&pool->lock);
      goto retry_acquire;
    }

    /* Unlock before performing I/O (ping check) */
    pthread_mutex_unlock(&pool->lock);

    if (corm_ping(db) == CORM_OK) {
      *out_db = db;
      return CORM_OK;
    }

    /* Ping failed: close connection and reduce current_open */
    corm_close(db);
    pthread_mutex_lock(&pool->lock);
    pool->current_open--;
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->lock);
    goto retry_acquire;
  }

  /* No idle connection: reserve slot and unlock before opening */
  pool->current_open++;
  pthread_mutex_unlock(&pool->lock);

  corm_t *db = NULL;
  corm_err_t err = CORM_ERR_GENERIC;
  int retried = 0;
  do {
    err = corm_open_with_config(pool->dsn, pool->config, &db);
    if (err == CORM_OK)
      break;
    corm_retry_sleep(50000000);
  } while (++retried < 3);

  if (err == CORM_OK) {
    *out_db = db;
    return CORM_OK;
  }

  /* Creation failed: rollback reserved slot */
  pthread_mutex_lock(&pool->lock);
  pool->current_open--;
  pthread_cond_signal(&pool->cond);
  pthread_mutex_unlock(&pool->lock);
  return err;
}

corm_err_t corm_pool_release(corm_pool_t *pool, corm_t *db) {
  if (!pool || !db)
    return CORM_ERR_NULL;
  pthread_mutex_lock(&pool->lock);

  /* Enforce max_idle_conns: close excess connections instead of enqueueing */
  if (pool->config.max_idle_conns > 0 &&
      pool->idle_count >= pool->config.max_idle_conns) {
    corm_close(db);
    pool->current_open--;
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->lock);
    return CORM_OK;
  }

  corm_pool_node_t *node = malloc(sizeof(corm_pool_node_t));
  if (!node) {
    corm_close(db);
    pool->current_open--;
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->lock);
    return CORM_ERR_NOMEM;
  }
  node->db = db;
  node->next = pool->idle_head;
  pool->idle_head = node;
  pool->idle_count++;

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
