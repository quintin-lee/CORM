#ifndef CORM_INTERNAL_H
#define CORM_INTERNAL_H

#include "corm_pub.h"
#include "hash.h"

typedef struct{corm_hash_t models_by_table;corm_hash_t models_by_name;}corm_registry_t;
corm_err_t corm_model_registry_init(corm_registry_t*reg);
void corm_model_registry_free(corm_registry_t*reg);

struct corm{
    corm_backend_t*backend;void*conn;corm_config_t config;corm_registry_t registry;
    corm_err_t last_err;char err_msg[512];char err_sql[1024];
    int64_t last_insert_id_val;int rows_affected_val;
};
#endif
