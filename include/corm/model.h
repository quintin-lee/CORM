#ifndef CORM_MODEL_H
#define CORM_MODEL_H

#include "types.h"
#include <stddef.h>

/** Field flags bitmask values. OR together and pass to corm_field_t.flags. */
#define CORM_FLAG_PRIMARY (1 << 0)  /**< Part of the primary key */
#define CORM_FLAG_AUTOINC (1 << 1)  /**< Auto-incrementing integer */
#define CORM_FLAG_NOT_NULL (1 << 2) /**< Column must not be NULL */
#define CORM_FLAG_UNIQUE (1 << 3)   /**< Column has a UNIQUE constraint */
#define CORM_FLAG_SOFT_DELETE                                                  \
  (1 << 4) /**< Delete sets a timestamp instead of removing row */

/** Describes a single column/field in an ORM model. */
typedef struct {
  const char *name;          /**< Column name in the database */
  corm_field_type_t type;    /**< CORM type (INT, STRING, BLOB, etc.) */
  size_t offset;             /**< Byte offset in the C struct (use offsetof) */
  size_t size;               /**< Storage size in bytes */
  unsigned int flags;        /**< Bitwise OR of CORM_FLAG_* values */
  const char *default_value; /**< Default SQL expression or NULL */
} corm_field_t;

struct corm;
/** Lifecycle hook for model operations. Return CORM_OK on success. */
typedef corm_err_t (*corm_hook_t)(struct corm *db, void *record);

/** ORM model definition mapping a C struct to a database table. */
struct corm_model {
  const char *table_name; /**< Database table name */
  size_t struct_size;     /**< sizeof() the C struct */
  corm_field_t *fields;   /**< Array of field descriptors */
  int field_count;        /**< Number of entries in fields[] */
  corm_field_t
      *primary_key; /**< Pointer to the primary-key field in fields[] */
  corm_hook_t before_create, after_create, before_update, after_update,
      before_delete, after_delete,
      after_find; /**< Lifecycle hooks (may be NULL) */
};
typedef struct corm_model corm_model_t;

/** Relation type for future relation-preloading support. */
typedef enum {
  CORM_REL_HAS_ONE,   /**< One-to-one relationship */
  CORM_REL_HAS_MANY,  /**< One-to-many relationship */
  CORM_REL_BELONGS_TO /**< Many-to-one relationship */
} corm_relation_type_t;

/** Describes a relationship between two models. */
typedef struct {
  const char *name;          /**< Relation name used in preload calls */
  corm_relation_type_t type; /**< Cardinality of the relationship */
  const char *target_table;  /**< Target model table name */
  const char *foreign_key;   /**< Foreign key column in the target table */
} corm_relation_t;

/** Convenience macro to define a corm_field_t from a struct type and field
 * name.
 *
 * Example: CORM_FIELD(user_t, name, CORM_STRING, CORM_FLAG_NOT_NULL, NULL) */
#define CORM_FIELD(st, fn, ft, fl, def)                                        \
  {.name = #fn,                                                                \
   .type = (ft),                                                               \
   .offset = offsetof(st, fn),                                                 \
   .size = sizeof(((st *)0)->fn),                                              \
   .flags = (fl),                                                              \
   .default_value = (def)}

#endif /* CORM_MODEL_H */
