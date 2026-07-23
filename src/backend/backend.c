#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "corm_pub.h"

/* ── Backend registry (thread-safe) ── */

#define CORM_MAX_BACKENDS 8

static corm_backend_t *g_backends[CORM_MAX_BACKENDS];
static int g_backend_count = 0;
static pthread_mutex_t g_backend_mutex = PTHREAD_MUTEX_INITIALIZER;

corm_err_t corm_register_backend(corm_backend_type_t type, corm_backend_t *backend) {
    pthread_mutex_lock(&g_backend_mutex);
    if (g_backend_count >= CORM_MAX_BACKENDS) {
        pthread_mutex_unlock(&g_backend_mutex);
        return CORM_ERR_GENERIC;
    }
    /* Check for duplicate */
    for (int i = 0; i < g_backend_count; i++) {
        if (g_backends[i]->type == type) {
            g_backends[i] = backend; /* replace */
            pthread_mutex_unlock(&g_backend_mutex);
            return CORM_OK;
        }
    }
    g_backends[g_backend_count++] = backend;
    pthread_mutex_unlock(&g_backend_mutex);
    return CORM_OK;
}

corm_backend_t *corm_get_backend(corm_backend_type_t type) {
    pthread_mutex_lock(&g_backend_mutex);
    for (int i = 0; i < g_backend_count; i++) {
        if (g_backends[i]->type == type) {
            corm_backend_t *result = g_backends[i];
            pthread_mutex_unlock(&g_backend_mutex);
            return result;
        }
    }
    pthread_mutex_unlock(&g_backend_mutex);
    return NULL;
}
