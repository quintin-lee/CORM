#ifndef CORM_MODEL_H
#define CORM_MODEL_H

#include "types.h"
#include <stddef.h>

#define CORM_FLAG_PRIMARY (1 << 0)
#define CORM_FLAG_AUTOINC (1 << 1)
#define CORM_FLAG_NOT_NULL (1 << 2)
#define CORM_FLAG_UNIQUE (1 << 3)

typedef struct {
  const char *name;
  corm_field_type_t type;
  size_t offset;
  size_t size;
  unsigned int flags;
  const char *default_value;
} corm_field_t;

struct corm;
typedef corm_err_t (*corm_hook_t)(struct corm *db, void *record);

struct corm_model {
  const char *table_name;
  size_t struct_size;
  corm_field_t *fields;
  int field_count;
  corm_field_t *primary_key;
  corm_hook_t before_create, after_create, before_update, after_update,
      before_delete, after_delete, after_find;
};
typedef struct corm_model corm_model_t;

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

#define CORM_FIELD(st, fn, ft, fl, def)                                        \
  {.name = #fn,                                                                \
   .type = (ft),                                                               \
   .offset = offsetof(st, fn),                                                 \
   .size = sizeof(((st *)0)->fn),                                              \
   .flags = (fl),                                                              \
   .default_value = (def)}

#endif /* CORM_MODEL_H */
