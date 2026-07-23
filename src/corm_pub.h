#ifndef CORM_PUB_H
#define CORM_PUB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ── Error codes ── */
typedef enum {
    CORM_OK=0, CORM_ERR_GENERIC=-1, CORM_ERR_NOMEM=-2, CORM_ERR_NOTFOUND=-3,
    CORM_ERR_DUP=-4, CORM_ERR_BACKEND=-5, CORM_ERR_TYPE=-6, CORM_ERR_NULL=-7,
    CORM_ERR_BOUNDS=-8, CORM_ERR_MISMATCH=-9,
} corm_err_t;

typedef enum { CORM_BACKEND_SQLITE, CORM_BACKEND_MYSQL, CORM_BACKEND_POSTGRES } corm_backend_type_t;
typedef enum { CORM_INT, CORM_INT64, CORM_FLOAT, CORM_DOUBLE, CORM_STRING, CORM_TEXT, CORM_BLOB, CORM_BOOL } corm_field_type_t;

typedef struct{ corm_field_type_t type; bool is_null; union{ int64_t i; double f; char *s; struct{void*data;size_t len;}blob; bool b; }v; } corm_value_t;

struct corm; typedef struct corm corm_t;
struct corm_model; typedef struct corm_model corm_model_t;
struct corm_result; typedef struct corm_result corm_result_t;

#define CORM_FLAG_PRIMARY (1<<0)
#define CORM_FLAG_AUTOINC (1<<1)
#define CORM_FLAG_NOT_NULL (1<<2)
#define CORM_FLAG_UNIQUE (1<<3)

typedef struct{const char*name;corm_field_type_t type;size_t offset;size_t size;unsigned int flags;const char*default_value;}corm_field_t;
typedef corm_err_t(*corm_hook_t)(corm_t*db,void*record);

struct corm_model{
    const char*table_name;size_t struct_size;corm_field_t*fields;
    int field_count;corm_field_t*primary_key;
    corm_hook_t before_create,after_create,before_update,after_update,before_delete,after_delete,after_find;
};

#define CORM_FIELD(st,fn,ft,fl,def) {.name=#fn,.type=(ft),.offset=offsetof(st,fn),.size=sizeof(((st*)0)->fn),.flags=(fl),.default_value=(def)}

typedef struct{int max_open_conns;int max_idle_conns;int conn_max_lifetime_ms;int timeout_ms;bool verbose_logging;}corm_config_t;
#define CORM_DEFAULT_CONFIG {0,2,0,30000,false}

struct corm_result{
    int column_count;char**column_names;corm_field_type_t*column_types;
    corm_value_t**rows;int row_count;int current_row;int refcount;
};

typedef enum{CORM_OP_SELECT,CORM_OP_INSERT,CORM_OP_UPDATE,CORM_OP_DELETE}corm_query_op_t;

#include "internal/strbuf.h"

struct corm_query;
typedef struct corm_query corm_query_t;
struct corm_query{
    corm_t*db;corm_model_t*model;corm_query_op_t op;
    corm_strbuf_t select_cols,where,order,group,having,joins,set_clause;
    corm_value_t*params;int param_count,param_cap;int limit,offset;
};

/* Backend vtable */
typedef struct corm_backend{
    const char*name;corm_backend_type_t type;
    corm_err_t(*open)(corm_t*db,const char*dsn);
    corm_err_t(*close)(corm_t*db);
    corm_err_t(*ping)(corm_t*db);
    corm_err_t(*exec)(corm_t*db,const char*sql,corm_value_t*params,int param_count);
    corm_err_t(*query)(corm_t*db,const char*sql,corm_value_t*params,int param_count,struct corm_result**out);
    corm_err_t(*begin)(corm_t*db);
    corm_err_t(*commit)(corm_t*db);
    corm_err_t(*rollback)(corm_t*db);
    size_t(*escape_string)(corm_t*db,char*dst,const char*src,size_t len);
    int64_t(*last_insert_id)(corm_t*db);
    int(*rows_affected)(corm_t*db);
}corm_backend_t;

/* ── Public API declarations ── */
extern corm_err_t corm_open(const char*dsn,corm_t**out);
extern corm_err_t corm_open_with_config(const char*dsn,corm_config_t config,corm_t**out);
extern corm_err_t corm_close(corm_t*db);
extern corm_err_t corm_ping(corm_t*db);
extern corm_err_t corm_begin(corm_t*db);
extern corm_err_t corm_commit(corm_t*db);
extern corm_err_t corm_rollback(corm_t*db);
extern corm_err_t corm_savepoint(corm_t *db, const char *name);
extern corm_err_t corm_rollback_to(corm_t *db, const char *name);
extern corm_err_t corm_release_savepoint(corm_t *db, const char *name);
extern corm_err_t corm_exec(corm_t*db,const char*sql);
extern corm_err_t corm_raw(corm_t*db,const char*sql,corm_result_t**out);
extern const char*corm_err_string(corm_err_t err);
extern corm_err_t corm_last_error(corm_t*db,char*buf,size_t bufsz);
extern corm_err_t corm_auto_migrate(corm_t*db,corm_model_t*models[],int model_count);
extern corm_err_t corm_init(void);
extern corm_err_t corm_register_backend(corm_backend_type_t type,corm_backend_t*backend);
extern corm_backend_t*corm_get_backend(corm_backend_type_t type);
extern corm_err_t corm_register_model(corm_t*db,corm_model_t*model);
extern corm_model_t*corm_find_model(corm_t*db,const char*table_name);
extern corm_field_t*corm_find_field(corm_model_t*model,const char*field_name);
extern corm_value_t corm_field_get_value(void*record,corm_field_t*field);
extern void corm_field_set_value(void*record,corm_field_t*field,corm_value_t*val);
extern corm_result_t*corm_result_new(int column_count,int row_count);
extern void corm_result_retain(corm_result_t*r);
extern void corm_result_release(corm_result_t*r);
extern int corm_result_row_count(corm_result_t*r);
extern int corm_result_col_count(corm_result_t*r);
extern const char*corm_result_col_name(corm_result_t*r,int col);
extern corm_field_type_t corm_result_col_type(corm_result_t*r,int col);
extern corm_value_t*corm_result_value(corm_result_t*r,int row,int col);
extern bool corm_result_next(corm_result_t*r);
extern void corm_result_reset(corm_result_t*r);
extern int64_t corm_result_int(corm_result_t*r,int row,int col);
extern double corm_result_double(corm_result_t*r,int row,int col);
extern const char*corm_result_string(corm_result_t*r,int row,int col);
extern bool corm_result_bool(corm_result_t*r,int row,int col);
extern bool corm_result_is_null(corm_result_t*r,int row,int col);
extern corm_query_t*corm_query_new(corm_t*db,corm_model_t*model);
extern void corm_query_free(corm_query_t*q);
extern void corm_query_reset(corm_query_t*q);
extern corm_query_t*corm_query_op(corm_query_t*q,corm_query_op_t op);
extern corm_query_t*corm_query_select(corm_query_t*q,const char*columns);
extern corm_query_t*corm_query_where(corm_query_t*q,const char*condition,...);
extern corm_query_t*corm_query_or_where(corm_query_t*q,const char*condition,...);
extern corm_query_t*corm_query_join(corm_query_t*q,const char*join_clause);
extern corm_query_t*corm_query_order(corm_query_t*q,const char*order);
extern corm_query_t*corm_query_group(corm_query_t*q,const char*group);
extern corm_query_t*corm_query_having(corm_query_t*q,const char*condition);
extern corm_query_t*corm_query_limit(corm_query_t*q,int limit);
extern corm_query_t*corm_query_offset(corm_query_t*q,int offset);
extern corm_query_t*corm_query_bind(corm_query_t*q,corm_value_t val);
extern corm_query_t*corm_query_set(corm_query_t*q,const char*field,corm_value_t val);
typedef enum {
    CORM_REL_HAS_ONE,
    CORM_REL_HAS_MANY,
    CORM_REL_BELONGS_TO
} corm_relation_type_t;

typedef struct {
    const char *name;
    corm_relation_type_t type;
    const char *target_table;
    const char *foreign_key;
} corm_relation_t;

extern corm_query_t* corm_query_preload(corm_query_t *q, const char *relation_name);
extern corm_query_t*corm_query_set_raw(corm_query_t*q,const char*clause);
extern corm_err_t corm_find(corm_query_t*q,corm_result_t**out);
extern corm_err_t corm_first(corm_query_t*q,void*record);
extern corm_err_t corm_create(corm_query_t*q,void*record,int64_t*insert_id);
extern corm_err_t corm_update(corm_query_t*q,int*affected);
extern corm_err_t corm_delete(corm_query_t*q,int*affected);
extern corm_err_t corm_find_all(corm_t*db,corm_model_t*model,const char*where,void*records,int*count);
extern corm_err_t corm_create_one(corm_t*db,corm_model_t*model,void*record,int64_t*insert_id);
extern corm_err_t corm_create_batch(corm_t *db, corm_model_t *model, void *records, int count, int batch_size, int *inserted_count);

extern const char *corm_dialect_if_not_exists(corm_backend_type_t backend);
extern const char *corm_dialect_quote(corm_backend_type_t backend, const char *name);
extern const char *corm_dialect_autoinc(corm_backend_type_t backend);
extern void corm_dialect_type_name_str(corm_backend_type_t backend, corm_field_type_t type, size_t size, char *buf, size_t bufsz);
extern void corm_dialect_placeholder_str(corm_backend_type_t backend, int index, char *buf, size_t bufsz);
extern corm_err_t corm_build_sql(corm_query_t *q, corm_strbuf_t *sql, corm_backend_type_t bt);
#endif
