#ifndef CORM_RESULT_H
#define CORM_RESULT_H

#include "types.h"

/** Query result set. Use corm_result_release() when done. */
struct corm_result {
  int column_count;                /**< Number of columns */
  char **column_names;             /**< Array of column name strings (owned) */
  corm_field_type_t *column_types; /**< Array of column type enums */
  corm_value_t **rows;             /**< 2D array: rows[row][col] */
  int row_count;                   /**< Number of rows in the result set */
  int current_row; /**< Iterator position for corm_result_next() */
  int refcount;    /**< Shared-ownership reference count */
};
typedef struct corm_result corm_result_t;

/** Allocate a new result container (internal). Use backends to populate. */
extern corm_result_t *corm_result_new(int column_count, int row_count);
/** Increment reference count. */
extern void corm_result_retain(corm_result_t *r);
/** Decrement reference count and free when it reaches zero. */
extern void corm_result_release(corm_result_t *r);
/** Return number of rows in the result set. */
extern int corm_result_row_count(corm_result_t *r);
/** Return number of columns in the result set. */
extern int corm_result_col_count(corm_result_t *r);
/** Return the name of column `col` (0-indexed), or NULL on invalid index. */
extern const char *corm_result_col_name(corm_result_t *r, int col);
/** Return the field type of column `col` (0-indexed). */
extern corm_field_type_t corm_result_col_type(corm_result_t *r, int col);
/** Return a pointer to the value at (row, col). */
extern corm_value_t *corm_result_value(corm_result_t *r, int row, int col);
/** Advance to the next row. Returns true if a row is available, false at end.
 */
extern bool corm_result_next(corm_result_t *r);
/** Reset iterator to before the first row. */
extern void corm_result_reset(corm_result_t *r);
/** Convenience: return integer value at (row, col). */
extern int64_t corm_result_int(corm_result_t *r, int row, int col);
/** Convenience: return double value at (row, col). */
extern double corm_result_double(corm_result_t *r, int row, int col);
/** Convenience: return string value at (row, col), or NULL if NULL. */
extern const char *corm_result_string(corm_result_t *r, int row, int col);
/** Convenience: return boolean value at (row, col). */
extern bool corm_result_bool(corm_result_t *r, int row, int col);
/** Return true if the value at (row, col) is SQL NULL. */
extern bool corm_result_is_null(corm_result_t *r, int row, int col);

#endif /* CORM_RESULT_H */
