#ifndef CORM_POOL_H
#define CORM_POOL_H

#include "corm_pub.h"
#include <pthread.h>

typedef struct corm_pool_node {
    corm_t *db;
    struct corm_pool_node *next;
} corm_pool_node_t;

typedef struct {
    char dsn[512];
    corm_config_t config;
    int current_open;
    corm_pool_node_t *idle_head;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} corm_pool_t;

corm_err_t corm_pool_create(const char *dsn, corm_config_t config, corm_pool_t **out_pool);
corm_err_t corm_pool_acquire(corm_pool_t *pool, corm_t **out_db);
corm_err_t corm_pool_release(corm_pool_t *pool, corm_t *db);
void corm_pool_destroy(corm_pool_t *pool);

#endif
