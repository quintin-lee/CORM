#ifndef CORM_RESULT_H
#define CORM_RESULT_H

#include "types.h"

struct corm_result {
  int column_count;
  char **column_names;
  corm_field_type_t *column_types;
  corm_value_t **rows;
  int row_count;
  int current_row;
  int refcount;
};
typedef struct corm_result corm_result_t;

extern corm_result_t *corm_result_new(int column_count, int row_count);
extern void corm_result_retain(corm_result_t *r);
extern void corm_result_release(corm_result_t *r);
extern int corm_result_row_count(corm_result_t *r);
extern int corm_result_col_count(corm_result_t *r);
extern const char *corm_result_col_name(corm_result_t *r, int col);
extern corm_field_type_t corm_result_col_type(corm_result_t *r, int col);
extern corm_value_t *corm_result_value(corm_result_t *r, int row, int col);
extern bool corm_result_next(corm_result_t *r);
extern void corm_result_reset(corm_result_t *r);
extern int64_t corm_result_int(corm_result_t *r, int row, int col);
extern double corm_result_double(corm_result_t *r, int row, int col);
extern const char *corm_result_string(corm_result_t *r, int row, int col);
extern bool corm_result_bool(corm_result_t *r, int row, int col);
extern bool corm_result_is_null(corm_result_t *r, int row, int col);

#endif /* CORM_RESULT_H */
