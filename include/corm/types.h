#ifndef CORM_TYPES_H
#define CORM_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @file types.h
 * Fundamental type definitions for the CORM library.
 * Defines error codes, backend identifiers, field types, and the
 * runtime value type used to pass data between the library and
 * user code.
 */

/** Error codes returned by all CORM functions.
 *  CORM_OK (0) indicates success; negative values indicate errors. */
typedef enum {
  CORM_OK = 0,            /**< Success */
  CORM_ERR_GENERIC = -1,  /**< Unspecified error */
  CORM_ERR_NOMEM = -2,    /**< Memory allocation failure */
  CORM_ERR_NOTFOUND = -3, /**< Record or resource not found */
  CORM_ERR_DUP = -4,      /**< Duplicate entry (unique constraint) */
  CORM_ERR_BACKEND = -5,  /**< Database backend error */
  CORM_ERR_TYPE = -6,     /**< Type mismatch */
  CORM_ERR_NULL = -7,     /**< NULL argument provided where prohibited */
  CORM_ERR_BOUNDS = -8,   /**< Index or array out of bounds */
  CORM_ERR_MISMATCH = -9, /**< Schema or model mismatch */
} corm_err_t;

/** Supported database backends. */
typedef enum {
  CORM_BACKEND_SQLITE,  /**< SQLite3 */
  CORM_BACKEND_MYSQL,   /**< MySQL / MariaDB */
  CORM_BACKEND_POSTGRES /**< PostgreSQL */
} corm_backend_type_t;

/** Field data types used in model definitions and result sets. */
typedef enum {
  CORM_INT,    /**< 32-bit integer (stored as int in struct) */
  CORM_INT64,  /**< 64-bit integer */
  CORM_FLOAT,  /**< 32-bit float */
  CORM_DOUBLE, /**< 64-bit double */
  CORM_STRING, /**< Fixed-size char array (embedded in struct) */
  CORM_TEXT,   /**< Variable-length char pointer (char * in struct) */
  CORM_BLOB,   /**< Binary large object (inline buffer in struct) */
  CORM_BOOL    /**< Boolean (stored as bool) */
} corm_field_type_t;

/** Runtime value used to pass data between the library and user code.
 *  The active member is determined by `type`. For CORM_STRING, `v.s` points
 *  into the struct buffer (borrowed). For CORM_TEXT, `v.s` is a char pointer
 *  from the struct (ownership unspecified). For CORM_BLOB, `v.blob` contains
 *  a pointer and length. */
typedef struct {
  corm_field_type_t type; /**< discriminator */
  bool is_null;           /**< true if the value is SQL NULL */
  union {
    int64_t i; /**< used by CORM_INT, CORM_INT64 */
    double f;  /**< used by CORM_FLOAT, CORM_DOUBLE */
    char *s;   /**< used by CORM_STRING, CORM_TEXT */
    struct {
      void *data;
      size_t len;
    } blob; /**< used by CORM_BLOB */
    bool b; /**< used by CORM_BOOL */
  } v;
} corm_value_t;

#endif /* CORM_TYPES_H */
