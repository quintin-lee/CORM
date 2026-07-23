#ifndef CORM_PUBLIC_POOL_H
#define CORM_PUBLIC_POOL_H

#include "config.h"
#include "types.h"

struct corm;
struct corm_pool;
typedef struct corm_pool corm_pool_t;

extern corm_err_t corm_pool_create(const char *dsn, corm_config_t config,
                                   corm_pool_t **out_pool);
extern corm_err_t corm_pool_acquire(corm_pool_t *pool, struct corm **out_db);
extern corm_err_t corm_pool_release(corm_pool_t *pool, struct corm *db);
extern void corm_pool_destroy(corm_pool_t *pool);

#endif /* CORM_PUBLIC_POOL_H */
