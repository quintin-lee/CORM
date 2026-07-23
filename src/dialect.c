#include "corm_pub.h"
#include <string.h>

/* ── SQL dialect abstraction (thread-safe: all functions write to
 * caller-supplied buffers) ── */

void corm_dialect_placeholder_str(corm_backend_type_t backend, int index,
                                  char *buf, size_t bufsz) {
  switch (backend) {
  case CORM_BACKEND_POSTGRES:
    snprintf(buf, bufsz, "$%d", index + 1);
    break;
  case CORM_BACKEND_MYSQL:
  case CORM_BACKEND_SQLITE:
  default:
    strncpy(buf, "?", bufsz);
    buf[bufsz - 1] = '\0';
    break;
  }
}

const char *corm_dialect_quote(corm_backend_type_t backend, const char *name) {
  (void)name;
  switch (backend) {
  case CORM_BACKEND_POSTGRES:
    return "\"";
  case CORM_BACKEND_MYSQL:
    return "`";
  case CORM_BACKEND_SQLITE:
    return "\"";
  }
  return "";
}

const char *corm_dialect_autoinc(corm_backend_type_t backend) {
  switch (backend) {
  case CORM_BACKEND_SQLITE:
    return "INTEGER PRIMARY KEY AUTOINCREMENT";
  case CORM_BACKEND_MYSQL:
    return "AUTO_INCREMENT";
  case CORM_BACKEND_POSTGRES:
    return "SERIAL PRIMARY KEY";
  }
  return "";
}

void corm_dialect_type_name_str(corm_backend_type_t backend,
                                corm_field_type_t type, size_t size, char *buf,
                                size_t bufsz) {
  switch (backend) {
  case CORM_BACKEND_SQLITE:
    switch (type) {
    case CORM_INT:
    case CORM_INT64:
      strncpy(buf, "INTEGER", bufsz);
      break;
    case CORM_FLOAT:
    case CORM_DOUBLE:
      strncpy(buf, "REAL", bufsz);
      break;
    case CORM_STRING:
    case CORM_TEXT:
      strncpy(buf, "TEXT", bufsz);
      break;
    case CORM_BLOB:
      strncpy(buf, "BLOB", bufsz);
      break;
    case CORM_BOOL:
      strncpy(buf, "INTEGER", bufsz);
      break;
    }
    break;
  case CORM_BACKEND_MYSQL:
    switch (type) {
    case CORM_INT:
      strncpy(buf, "INT", bufsz);
      break;
    case CORM_INT64:
      strncpy(buf, "BIGINT", bufsz);
      break;
    case CORM_FLOAT:
      strncpy(buf, "FLOAT", bufsz);
      break;
    case CORM_DOUBLE:
      strncpy(buf, "DOUBLE", bufsz);
      break;
    case CORM_STRING:
      snprintf(buf, bufsz, "VARCHAR(%zu)",
               size > 0 && size < 65535 ? size : 255);
      break;
    case CORM_TEXT:
      strncpy(buf, "TEXT", bufsz);
      break;
    case CORM_BLOB:
      strncpy(buf, "BLOB", bufsz);
      break;
    case CORM_BOOL:
      strncpy(buf, "TINYINT(1)", bufsz);
      break;
    }
    break;
  case CORM_BACKEND_POSTGRES:
    switch (type) {
    case CORM_INT:
      strncpy(buf, "INTEGER", bufsz);
      break;
    case CORM_INT64:
      strncpy(buf, "BIGINT", bufsz);
      break;
    case CORM_FLOAT:
      strncpy(buf, "REAL", bufsz);
      break;
    case CORM_DOUBLE:
      strncpy(buf, "DOUBLE PRECISION", bufsz);
      break;
    case CORM_STRING:
      snprintf(buf, bufsz, "VARCHAR(%zu)",
               size > 0 && size < 65535 ? size : 255);
      break;
    case CORM_TEXT:
      strncpy(buf, "TEXT", bufsz);
      break;
    case CORM_BLOB:
      strncpy(buf, "BYTEA", bufsz);
      break;
    case CORM_BOOL:
      strncpy(buf, "BOOLEAN", bufsz);
      break;
    }
    break;
  default:
    strncpy(buf, "TEXT", bufsz);
    break;
  }
  buf[bufsz - 1] = '\0';
}

const char *corm_dialect_if_not_exists(corm_backend_type_t backend) {
  (void)backend;
  return "IF NOT EXISTS";
}
