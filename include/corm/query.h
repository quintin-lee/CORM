#ifndef CORM_QUERY_H
#define CORM_QUERY_H

#include "internal/strbuf.h"
#include "model.h"
#include "result.h"
#include "types.h"

typedef enum {
  CORM_OP_SELECT,
  CORM_OP_INSERT,
  CORM_OP_UPDATE,
  CORM_OP_DELETE
} corm_query_op_t;

struct corm;

struct corm_query {
  struct corm *db;
  corm_model_t *model;
  corm_query_op_t op;
  corm_strbuf_t select_cols, where, order, group, having, joins, set_clause;
  corm_value_t *params;
  int param_count, param_cap;
  int limit, offset;
  bool unscoped;
};
typedef struct corm_query corm_query_t;

extern corm_query_t *corm_query_unscoped(corm_query_t *q);

extern corm_query_t *corm_query_new(struct corm *db, corm_model_t *model);
extern void corm_query_free(corm_query_t *q);
extern void corm_query_reset(corm_query_t *q);
extern corm_query_t *corm_query_op(corm_query_t *q, corm_query_op_t op);
extern corm_query_t *corm_query_select(corm_query_t *q, const char *columns);
extern corm_query_t *corm_query_where(corm_query_t *q, const char *condition,
                                      ...);
extern corm_query_t *corm_query_or_where(corm_query_t *q, const char *condition,
                                         ...);
extern corm_query_t *corm_query_where_null(corm_query_t *q, const char *field);
extern corm_query_t *corm_query_where_not_null(corm_query_t *q,
                                               const char *field);
extern corm_query_t *corm_query_where_in(corm_query_t *q, const char *field,
                                         corm_value_t *vals, int count);
extern corm_query_t *corm_query_where_between(corm_query_t *q,
                                              const char *field,
                                              corm_value_t min_val,
                                              corm_value_t max_val);
extern corm_query_t *corm_query_join(corm_query_t *q, const char *join_clause);
extern corm_query_t *corm_query_order(corm_query_t *q, const char *order);
extern corm_query_t *corm_query_group(corm_query_t *q, const char *group);
extern corm_query_t *corm_query_having(corm_query_t *q, const char *condition);
extern corm_query_t *corm_query_limit(corm_query_t *q, int limit);
extern corm_query_t *corm_query_offset(corm_query_t *q, int offset);
extern corm_query_t *corm_query_bind(corm_query_t *q, corm_value_t val);
extern corm_query_t *corm_query_set(corm_query_t *q, const char *field,
                                    corm_value_t val);
extern corm_query_t *corm_query_set_raw(corm_query_t *q, const char *clause);
extern corm_query_t *corm_query_preload(corm_query_t *q,
                                        const char *relation_name)
    __attribute__((deprecated("preload is not implemented; will be removed")));
extern corm_err_t corm_find(corm_query_t *q, corm_result_t **out);
extern corm_err_t corm_first(corm_query_t *q, void *record);
extern corm_err_t corm_create(corm_query_t *q, void *record,
                              int64_t *insert_id);
extern corm_err_t corm_update(corm_query_t *q, int *affected);
extern corm_err_t corm_delete(corm_query_t *q, int *affected);

#endif /* CORM_QUERY_H */
