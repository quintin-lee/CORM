#include <string.h>
#include "corm_pub.h"

/* ── SQL dialect abstraction ── */

const char *corm_dialect_placeholder(corm_backend_type_t backend, int index) {
    switch (backend) {
        case CORM_BACKEND_POSTGRES: {
            /* PostgreSQL uses $1, $2, ... */
            static char buf[16];
            snprintf(buf, sizeof(buf), "$%d", index + 1);
            return buf;
        }
        case CORM_BACKEND_MYSQL:
        case CORM_BACKEND_SQLITE:
        default:
            return "?";
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

const char *corm_dialect_type_name(corm_backend_type_t backend, corm_field_type_t type, size_t size) {
    switch (backend) {
        case CORM_BACKEND_SQLITE:
            /* SQLite is flexible; use generic types */
            switch (type) {
                case CORM_INT:
                case CORM_INT64: return "INTEGER";
                case CORM_FLOAT:
                case CORM_DOUBLE: return "REAL";
                case CORM_STRING: return "TEXT";
                case CORM_TEXT:   return "TEXT";
                case CORM_BLOB:   return "BLOB";
                case CORM_BOOL:   return "INTEGER";
            }
            break;
        case CORM_BACKEND_MYSQL:
            switch (type) {
                case CORM_INT:    return "INT";
                case CORM_INT64:  return "BIGINT";
                case CORM_FLOAT:  return "FLOAT";
                case CORM_DOUBLE: return "DOUBLE";
                case CORM_STRING: {
                    static char buf[32];
                    snprintf(buf, sizeof(buf), "VARCHAR(%zu)", size > 0 && size < 65535 ? size : 255);
                    return buf;
                }
                case CORM_TEXT:   return "TEXT";
                case CORM_BLOB:   return "BLOB";
                case CORM_BOOL:   return "TINYINT(1)";
            }
            break;
        case CORM_BACKEND_POSTGRES:
            switch (type) {
                case CORM_INT:    return "INTEGER";
                case CORM_INT64:  return "BIGINT";
                case CORM_FLOAT:  return "REAL";
                case CORM_DOUBLE: return "DOUBLE PRECISION";
                case CORM_STRING: {
                    static char buf[32];
                    snprintf(buf, sizeof(buf), "VARCHAR(%zu)", size > 0 && size < 65535 ? size : 255);
                    return buf;
                }
                case CORM_TEXT:   return "TEXT";
                case CORM_BLOB:   return "BYTEA";
                case CORM_BOOL:   return "BOOLEAN";
            }
            break;
    }
    return "TEXT";
}

const char *corm_dialect_limit_offset(corm_backend_type_t backend) {
    switch (backend) {
        case CORM_BACKEND_MYSQL:
        case CORM_BACKEND_POSTGRES:
            return "LIMIT ? OFFSET ?";
        case CORM_BACKEND_SQLITE:
            return "LIMIT ? OFFSET ?";
    }
    return "LIMIT ? OFFSET ?";
}

const char *corm_dialect_if_not_exists(corm_backend_type_t backend) {
    (void)backend;
    return "IF NOT EXISTS";
}
