#include <stdlib.h>
#include <string.h>
#include "corm_pub.h"

/* ── Backend registry ── */

#define CORM_MAX_BACKENDS 8

static corm_backend_t *g_backends[CORM_MAX_BACKENDS];
static int g_backend_count = 0;

corm_err_t corm_register_backend(corm_backend_type_t type, corm_backend_t *backend) {
    if (g_backend_count >= CORM_MAX_BACKENDS) return CORM_ERR_GENERIC;
    /* Check for duplicate */
    for (int i = 0; i < g_backend_count; i++) {
        if (g_backends[i]->type == type) {
            g_backends[i] = backend; /* replace */
            return CORM_OK;
        }
    }
    g_backends[g_backend_count++] = backend;
    return CORM_OK;
}

corm_backend_t *corm_get_backend(corm_backend_type_t type) {
    for (int i = 0; i < g_backend_count; i++) {
        if (g_backends[i]->type == type)
            return g_backends[i];
    }
    return NULL;
}
