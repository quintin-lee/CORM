#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CORM_HAVE_MYSQL
#include <mysql/mysql.h>
#endif

#include "corm_pub.h"

#include "../internal/corm_internal.h"

/* ── MySQL backend ── */

#ifdef CORM_HAVE_MYSQL

static corm_err_t mysql_open(corm_t *db, const char *dsn) {
  /* Parse DSN: [user[:pass]@][host][:port][/dbname] */
  char user[256] = "root";
  char pass[256] = "";
  char host[256] = "127.0.0.1";
  int port = 3306;
  char dbname[256] = "";

  /* Very simple parsing for common formats */
  const char *p = dsn;
  char buf[1024];
  strncpy(buf, dsn, sizeof(buf) - 1);

  /* Check for user@host or user:pass@host */
  char *at = strchr(buf, '@');
  if (at) {
    *at = '\0';
    char *colon = strchr(buf, ':');
    if (colon) {
      strncpy(user, buf, (size_t)(colon - buf));
      strncpy(pass, colon + 1, sizeof(pass) - 1);
    } else {
      strncpy(user, buf, sizeof(user) - 1);
    }
    p = at + 1;
  } else {
    p = buf;
  }

  /* Parse host:port/dbname */
  char *slash = strchr(p, '/');
  if (slash) {
    strncpy(dbname, slash + 1, sizeof(dbname) - 1);
    size_t hostlen = (size_t)(slash - p);
    if (hostlen > 0 && hostlen < sizeof(host)) {
      strncpy(host, p, hostlen);
      host[hostlen] = '\0';
    }
    /* Check for host:port */
    char *hcolon = strchr(host, ':');
    if (hcolon) {
      *hcolon = '\0';
      port = atoi(hcolon + 1);
    }
  } else {
    /* Check for :port */
    char *hcolon = strchr(p, ':');
    if (hcolon) {
      size_t hostlen = (size_t)(hcolon - p);
      strncpy(host, p, hostlen);
      host[hostlen] = '\0';
      port = atoi(hcolon + 1);
    } else {
      strncpy(host, p, sizeof(host) - 1);
    }
  }

  MYSQL *handle = mysql_init(NULL);
  if (!handle) {
    snprintf(db->err_msg, sizeof(db->err_msg), "mysql_init failed");
    return CORM_ERR_NOMEM;
  }

  unsigned int conn_timeout = db->config.timeout_ms > 0
                                  ? (unsigned int)(db->config.timeout_ms / 1000)
                                  : 30;
  mysql_options(handle, MYSQL_OPT_CONNECT_TIMEOUT, &conn_timeout);

  if (!mysql_real_connect(handle, host, user, pass, dbname[0] ? dbname : NULL,
                          (unsigned int)port, NULL, 0)) {
    snprintf(db->err_msg, sizeof(db->err_msg), "%s", mysql_error(handle));
    mysql_close(handle);
    return CORM_ERR_BACKEND;
  }

  db->conn = handle;
  return CORM_OK;
}

static corm_err_t corm_mysql_close(corm_t *db) {
  MYSQL *handle = (MYSQL *)db->conn;
  if (handle) {
    mysql_close(handle);
    db->conn = NULL;
  }
  return CORM_OK;
}

static corm_err_t corm_mysql_ping(corm_t *db) {
  MYSQL *handle = (MYSQL *)db->conn;
  if (!handle)
    return CORM_ERR_BACKEND;
  return mysql_ping(handle) == 0 ? CORM_OK : CORM_ERR_BACKEND;
}

static corm_err_t mysql_exec(corm_t *db, const char *sql, corm_value_t *params,
                             int param_count) {
  MYSQL *handle = (MYSQL *)db->conn;
  (void)params;
  (void)param_count;

  if (mysql_query(handle, sql) != 0) {
    snprintf(db->err_msg, sizeof(db->err_msg), "%s", mysql_error(handle));
    return CORM_ERR_BACKEND;
  }
  return CORM_OK;
}

static corm_err_t corm_mysql_query(corm_t *db, const char *sql,
                                   corm_value_t *params, int param_count,
                                   corm_result_t **out) {
  MYSQL *handle = (MYSQL *)db->conn;
  (void)params;
  (void)param_count;

  if (mysql_query(handle, sql) != 0) {
    snprintf(db->err_msg, sizeof(db->err_msg), "%s", mysql_error(handle));
    return CORM_ERR_BACKEND;
  }

  MYSQL_RES *mysql_res = mysql_store_result(handle);
  if (!mysql_res) {
    /* Not a SELECT-like query */
    *out = NULL;
    return CORM_OK;
  }

  int col_count = mysql_num_fields(mysql_res);
  int row_count = (int)mysql_num_rows(mysql_res);

  corm_result_t *res = corm_result_new(col_count, row_count);
  if (!res) {
    mysql_free_result(mysql_res);
    return CORM_ERR_NOMEM;
  }

  /* Column info */
  MYSQL_FIELD *fields = mysql_fetch_fields(mysql_res);
  for (int i = 0; i < col_count; i++) {
    res->column_names[i] = strdup(fields[i].name);
    switch (fields[i].type) {
    case MYSQL_TYPE_TINY:
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_INT24:
    case MYSQL_TYPE_LONGLONG:
      res->column_types[i] = CORM_INT64;
      break;
    case MYSQL_TYPE_FLOAT:
    case MYSQL_TYPE_DOUBLE:
    case MYSQL_TYPE_DECIMAL:
      res->column_types[i] = CORM_DOUBLE;
      break;
    case MYSQL_TYPE_BLOB:
      res->column_types[i] = CORM_BLOB;
      break;
    default:
      res->column_types[i] = CORM_TEXT;
      break;
    }
  }

  /* Rows */
  MYSQL_ROW row;
  int r = 0;
  while ((row = mysql_fetch_row(mysql_res))) {
    unsigned long *lengths = mysql_fetch_lengths(mysql_res);
    for (int i = 0; i < col_count; i++) {
      corm_value_t *v = &res->rows[r][i];
      v->type = res->column_types[i];
      if (row[i] == NULL) {
        v->is_null = true;
        continue;
      }
      switch (v->type) {
      case CORM_INT64:
        v->v.i = strtoll(row[i], NULL, 10);
        break;
      case CORM_DOUBLE:
        v->v.f = strtod(row[i], NULL);
        break;
      case CORM_TEXT:
        v->v.s = strdup(row[i]);
        break;
      case CORM_BLOB:
        v->v.blob.data = malloc(lengths[i]);
        if (v->v.blob.data) {
          memcpy(v->v.blob.data, row[i], lengths[i]);
          v->v.blob.len = lengths[i];
        }
        break;
      default:
        v->v.s = strdup(row[i]);
        v->type = CORM_TEXT;
        break;
      }
    }
    r++;
  }

  mysql_free_result(mysql_res);
  *out = res;
  return CORM_OK;
}

static corm_err_t corm_mysql_begin(corm_t *db) {
  return mysql_exec(db, "START TRANSACTION", NULL, 0);
}

static corm_err_t corm_mysql_commit(corm_t *db) {
  return mysql_exec(db, "COMMIT", NULL, 0);
}

static corm_err_t corm_mysql_rollback(corm_t *db) {
  return mysql_exec(db, "ROLLBACK", NULL, 0);
}

static size_t mysql_escape(corm_t *db, char *dst, const char *src, size_t len) {
  MYSQL *handle = (MYSQL *)db->conn;
  if (!handle || !dst)
    return len * 2 + 1;
  return (size_t)mysql_real_escape_string(handle, dst, src, (unsigned long)len);
}

static int64_t mysql_last_id(corm_t *db) {
  MYSQL *handle = (MYSQL *)db->conn;
  return (int64_t)mysql_insert_id(handle);
}

static int mysql_affected(corm_t *db) {
  MYSQL *handle = (MYSQL *)db->conn;
  return (int)mysql_affected_rows(handle);
}

static corm_backend_t mysql_backend = {
    .name = "mysql",
    .type = CORM_BACKEND_MYSQL,
    .open = mysql_open,
    .close = corm_mysql_close,
    .ping = corm_mysql_ping,
    .exec = mysql_exec,
    .query = corm_mysql_query,
    .begin = corm_mysql_begin,
    .commit = corm_mysql_commit,
    .rollback = corm_mysql_rollback,
    .escape_string = mysql_escape,
    .last_insert_id = mysql_last_id,
    .rows_affected = mysql_affected,
};

__attribute__((constructor)) static void mysql_register(void) {
  corm_register_backend(CORM_BACKEND_MYSQL, &mysql_backend);
}

corm_err_t corm_register_mysql_backend(void) {
  return corm_register_backend(CORM_BACKEND_MYSQL, &mysql_backend);
}

#else /* CORM_HAVE_MYSQL */

static corm_backend_t mysql_stub_backend = {
    .name = "mysql (unavailable)",
    .type = CORM_BACKEND_MYSQL,
};

__attribute__((constructor)) static void mysql_stub_register(void) {
  corm_register_backend(CORM_BACKEND_MYSQL, &mysql_stub_backend);
}

corm_err_t corm_register_mysql_backend(void) {
  return corm_register_backend(CORM_BACKEND_MYSQL, &mysql_stub_backend);
}

#endif /* CORM_HAVE_MYSQL */
