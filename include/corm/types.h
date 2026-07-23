#ifndef CORM_TYPES_H
#define CORM_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ── Error codes ── */
typedef enum {
    CORM_OK = 0,
    CORM_ERR_GENERIC = -1,
    CORM_ERR_NOMEM = -2,
    CORM_ERR_NOTFOUND = -3,
    CORM_ERR_DUP = -4,
    CORM_ERR_BACKEND = -5,
    CORM_ERR_TYPE = -6,
    CORM_ERR_NULL = -7,
    CORM_ERR_BOUNDS = -8,
    CORM_ERR_MISMATCH = -9,
} corm_err_t;

typedef enum {
    CORM_BACKEND_SQLITE,
    CORM_BACKEND_MYSQL,
    CORM_BACKEND_POSTGRES
} corm_backend_type_t;

typedef enum {
    CORM_INT,
    CORM_INT64,
    CORM_FLOAT,
    CORM_DOUBLE,
    CORM_STRING,
    CORM_TEXT,
    CORM_BLOB,
    CORM_BOOL
} corm_field_type_t;

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

#endif /* CORM_TYPES_H */
