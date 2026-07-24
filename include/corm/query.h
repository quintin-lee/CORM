#ifndef CORM_QUERY_H
#define CORM_QUERY_H

#include "internal/strbuf.h"
#include "model.h"
#include "result.h"
#include "types.h"

/** Query operation type — set automatically by query-building functions. */
typedef enum {
  CORM_OP_SELECT, /**< SELECT query */
  CORM_OP_INSERT, /**< INSERT query */
  CORM_OP_UPDATE, /**< UPDATE query */
  CORM_OP_DELETE  /**< DELETE query */
} corm_query_op_t;

struct corm;

/** Fluent query builder state. Created by corm_query_new(), freed by
 * corm_query_free(). All builder methods return the same pointer for chaining.
 */
struct corm_query {
  struct corm *db;     /**< Database connection */
  corm_model_t *model; /**< Target ORM model */
  corm_query_op_t op;  /**< Current operation type */
  corm_strbuf_t select_cols, where, order, group, having, joins,
      set_clause;       /**< SQL clause buffers */
  corm_value_t *params; /**< Bound parameter values (deep-copied on bind) */
  int param_count,
      param_cap;        /**< Number of bound params / allocated capacity */
  int limit, offset;    /**< LIMIT and OFFSET values (0 = none) */
  bool unscoped;        /**< Skip soft-delete WHERE clause when true */
  bool distinct;        /**< Enable DISTINCT for SELECT queries */
  char preload_rel[64]; /**< Target relation table name for eager preloading */
};
typedef struct corm_query corm_query_t;

/** Enable DISTINCT for SELECT queries. */
extern corm_query_t *corm_query_distinct(corm_query_t *q);

/** Exclude soft-delete filter from subsequent queries. */
extern corm_query_t *corm_query_unscoped(corm_query_t *q);

/** Create a new query builder for the given model and database. */
extern corm_query_t *corm_query_new(struct corm *db, corm_model_t *model);
/** Free all resources owned by the query, including deep-copied params. */
extern void corm_query_free(corm_query_t *q);
/** Reset query state for reuse — clears clauses, params, and options. */
extern void corm_query_reset(corm_query_t *q);
/** Override the query operation type. */
extern corm_query_t *corm_query_op(corm_query_t *q, corm_query_op_t op);
/** Set columns for SELECT: "id, name, age" or "*". */
extern corm_query_t *corm_query_select(corm_query_t *q, const char *columns);
/** Add a WHERE condition. Use ? placeholders and corm_query_bind() for values.
 *  Variadic params are intentionally unsupported. */
extern corm_query_t *corm_query_where(corm_query_t *q, const char *condition,
                                      ...);
/** Add an OR WHERE condition. */
extern corm_query_t *corm_query_or_where(corm_query_t *q, const char *condition,
                                         ...);
/** Add a WHERE field IS NULL clause. */
extern corm_query_t *corm_query_where_null(corm_query_t *q, const char *field);
/** Add a WHERE field IS NOT NULL clause. */
extern corm_query_t *corm_query_where_not_null(corm_query_t *q,
                                               const char *field);
/** Add a WHERE field IN (?, ?, ...) clause. */
extern corm_query_t *corm_query_where_in(corm_query_t *q, const char *field,
                                         corm_value_t *vals, int count);
/** Add a WHERE field BETWEEN ? AND ? clause. */
extern corm_query_t *corm_query_where_between(corm_query_t *q,
                                              const char *field,
                                              corm_value_t min_val,
                                              corm_value_t max_val);
/** Add a JOIN clause: "LEFT JOIN orders ON orders.user_id = users.id". */
extern corm_query_t *corm_query_join(corm_query_t *q, const char *join_clause);
/** Add ORDER BY clause. */
extern corm_query_t *corm_query_order(corm_query_t *q, const char *order);
/** Add GROUP BY clause. */
extern corm_query_t *corm_query_group(corm_query_t *q, const char *group);
/** Add HAVING clause. */
extern corm_query_t *corm_query_having(corm_query_t *q, const char *condition);
/** Set LIMIT. */
extern corm_query_t *corm_query_limit(corm_query_t *q, int limit);
/** Set OFFSET. */
extern corm_query_t *corm_query_offset(corm_query_t *q, int offset);
/** Bind a parameter value for the next ? placeholder. Data is deep-copied. */
extern corm_query_t *corm_query_bind(corm_query_t *q, corm_value_t val);
/** Set a column value for UPDATE: "name = ?". Binds val internally. */
extern corm_query_t *corm_query_set(corm_query_t *q, const char *field,
                                    corm_value_t val);
/** Set the entire SET clause verbatim (no placeholder conversion). */
extern corm_query_t *corm_query_set_raw(corm_query_t *q, const char *clause);
/** Preload relation by table name for eager loading. */
extern corm_query_t *corm_query_preload(corm_query_t *q,
                                        const char *relation_name);
/** Execute a SELECT and return a result set. */
extern corm_err_t corm_find(corm_query_t *q, corm_result_t **out);
/** Execute a SELECT and populate a single record (with WHERE LIMIT 1). */
extern corm_err_t corm_first(corm_query_t *q, void *record);
/** Execute an INSERT using the query's model. */
extern corm_err_t corm_create(corm_query_t *q, void *record,
                              int64_t *insert_id);
/** Execute an UPDATE using the query's SET clauses and WHERE. */
extern corm_err_t corm_update(corm_query_t *q, int *affected);
/** Execute a DELETE using the query's WHERE (or soft-delete if the model has
 * CORM_FLAG_SOFT_DELETE). */
extern corm_err_t corm_delete(corm_query_t *q, int *affected);
/** Execute a COUNT(*) query matching current query filters. */
extern corm_err_t corm_query_count(corm_query_t *q, int64_t *out_count);

#endif /* CORM_QUERY_H */
