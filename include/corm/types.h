#ifndef CORM_TYPES_H
#define CORM_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Error codes ── */
typedef enum {
    CORM_OK            =  0,
    CORM_ERR_GENERIC   = -1,
    CORM_ERR_NOMEM     = -2,
    CORM_ERR_NOTFOUND  = -3,
    CORM_ERR_DUP       = -4,
    CORM_ERR_BACKEND   = -5,
    CORM_ERR_TYPE      = -6,
    CORM_ERR_NULL      = -7,
    CORM_ERR_BOUNDS    = -8,
    CORM_ERR_MISMATCH  = -9,
} corm_err_t;

/* ── Backend type ── */
typedef enum {
    CORM_BACKEND_SQLITE,
    CORM_BACKEND_MYSQL,
    CORM_BACKEND_POSTGRES,
} corm_backend_type_t;

/* ── Field types ── */
typedef enum {
    CORM_INT,
    CORM_INT64,
    CORM_FLOAT,
    CORM_DOUBLE,
    CORM_STRING,   /* fixed-size char[N] */
    CORM_TEXT,      /* dynamic char* */
    CORM_BLOB,
    CORM_BOOL,
} corm_field_type_t;

/* ── Value ── */
typedef struct {
    corm_field_type_t type;
    bool is_null;
    union {
        int64_t i;
        double f;
        char *s;
        struct { void *data; size_t len; } blob;
        bool b;
    } v;
} corm_value_t;

/* ── Forward declarations ── */
typedef struct corm       corm_t;
typedef struct corm_model corm_model_t;
typedef struct corm_query corm_query_t;
typedef struct corm_result corm_result_t;

#ifdef __cplusplus
}
#endif

#endif /* CORM_TYPES_H */
