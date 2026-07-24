#ifndef CORM_PUBLIC_POOL_H
#define CORM_PUBLIC_POOL_H

#include "config.h"
#include "types.h"

struct corm;
struct corm_pool;

/** Opaque connection pool handle. Created by corm_pool_create(). */
typedef struct corm_pool corm_pool_t;

/** Create a connection pool for the given DSN.
 *
 * @param dsn      Database DSN (e.g. "sqlite3://:memory:")
 * @param config   Pool configuration (max connections, timeouts, etc.)
 * @param out_pool Receives the new pool handle
 * @return CORM_OK on success, or an error code */
extern corm_err_t corm_pool_create(const char *dsn, corm_config_t config,
                                   corm_pool_t **out_pool);

/** Acquire a connection from the pool. Blocks up to config.timeout_ms.
 *
 * @param pool   Pool handle from corm_pool_create()
 * @param out_db Receives a borrowed corm_t pointer (do NOT call corm_close)
 * @return CORM_OK on success */
extern corm_err_t corm_pool_acquire(corm_pool_t *pool, struct corm **out_db);

/** Return a connection to the pool for reuse.
 *
 * @param pool Pool handle
 * @param db   Connection previously acquired via corm_pool_acquire() */
extern corm_err_t corm_pool_release(corm_pool_t *pool, struct corm *db);

/** Destroy the pool and all idle connections. Active connections become
 * invalid.
 *
 * @param pool Pool handle (may be NULL) */
extern void corm_pool_destroy(corm_pool_t *pool);

#endif /* CORM_PUBLIC_POOL_H */
