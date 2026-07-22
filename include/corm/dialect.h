#ifndef CORM_DIALECT_H
#define CORM_DIALECT_H

#include "types.h"
#include "model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SQL dialect abstraction functions */
const char *corm_dialect_placeholder(corm_backend_type_t backend, int index);
const char *corm_dialect_quote(corm_backend_type_t backend, const char *name);
const char *corm_dialect_autoinc(corm_backend_type_t backend);
const char *corm_dialect_type_name(corm_backend_type_t backend, corm_field_type_t type, size_t size);
const char *corm_dialect_limit_offset(corm_backend_type_t backend);
const char *corm_dialect_if_not_exists(corm_backend_type_t backend);

/* SQL builder */
corm_err_t corm_build_sql(corm_query_t *q, corm_strbuf_t *sql);

#ifdef __cplusplus
}
#endif

#endif /* CORM_DIALECT_H */
