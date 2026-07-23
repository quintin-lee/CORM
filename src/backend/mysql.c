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

/* Bind corm_value_t params into a MYSQL_BIND array (caller must free) */
static MYSQL_BIND *mysql_bind_params(corm_value_t *params, int param_count) {
  MYSQL_BIND *bind =
      (MYSQL_BIND *)calloc((size_t)param_count, sizeof(MYSQL_BIND));
  if (!bind)
    return NULL;
  for (int i = 0; i < param_count; i++) {
    if (params[i].is_null) {
      bind[i].buffer_type = MYSQL_TYPE_NULL;
    } else {
      switch (params[i].type) {
      case CORM_INT:
      case CORM_INT64:
        bind[i].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[i].buffer = (char *)&params[i].v.i;
        break;
      case CORM_FLOAT:
      case CORM_DOUBLE:
        bind[i].buffer_type = MYSQL_TYPE_DOUBLE;
        bind[i].buffer = (char *)&params[i].v.f;
        break;
      case CORM_STRING:
      case CORM_TEXT:
        bind[i].buffer_type = MYSQL_TYPE_STRING;
        bind[i].buffer = params[i].v.s;
        bind[i].buffer_length = (unsigned long)strlen(params[i].v.s);
        break;
      case CORM_BOOL:
        bind[i].buffer_type = MYSQL_TYPE_TINY;
        bind[i].buffer = (char *)&params[i].v.b;
        break;
      case CORM_BLOB:
        bind[i].buffer_type = MYSQL_TYPE_BLOB;
        bind[i].buffer = params[i].v.blob.data;
        bind[i].buffer_length = (unsigned long)params[i].v.blob.len;
        break;
      default:
        bind[i].buffer_type = MYSQL_TYPE_STRING;
        bind[i].buffer = params[i].v.s;
        bind[i].buffer_length =
            params[i].v.s ? (unsigned long)strlen(params[i].v.s) : 0;
        break;
      }
    }
  }
  return bind;
}

static corm_err_t mysql_exec(corm_t *db, const char *sql, corm_value_t *params,
                             int param_count) {
  MYSQL *handle = (MYSQL *)db->conn;

  if (param_count == 0) {
    if (mysql_query(handle, sql) != 0) {
      snprintf(db->err_msg, sizeof(db->err_msg), "%s", mysql_error(handle));
      return CORM_ERR_BACKEND;
    }
    db->last_insert_id_val = (int64_t)mysql_insert_id(handle);
    db->rows_affected_val = (int)mysql_affected_rows(handle);
    return CORM_OK;
  }

  MYSQL_STMT *stmt = mysql_stmt_init(handle);
  if (!stmt) {
    snprintf(db->err_msg, sizeof(db->err_msg), "mysql_stmt_init failed");
    return CORM_ERR_NOMEM;
  }

  if (mysql_stmt_prepare(stmt, sql, (unsigned long)strlen(sql)) != 0) {
    snprintf(db->err_msg, sizeof(db->err_msg), "%s", mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    return CORM_ERR_BACKEND;
  }

  MYSQL_BIND *bind = mysql_bind_params(params, param_count);
  if (!bind) {
    mysql_stmt_close(stmt);
    return CORM_ERR_NOMEM;
  }

  if (mysql_stmt_bind_param(stmt, bind) != 0) {
    snprintf(db->err_msg, sizeof(db->err_msg), "%s", mysql_stmt_error(stmt));
    free(bind);
    mysql_stmt_close(stmt);
    return CORM_ERR_BACKEND;
  }

  if (mysql_stmt_execute(stmt) != 0) {
    snprintf(db->err_msg, sizeof(db->err_msg), "%s", mysql_stmt_error(stmt));
    free(bind);
    mysql_stmt_close(stmt);
    return CORM_ERR_BACKEND;
  }

  db->last_insert_id_val = (int64_t)mysql_stmt_insert_id(stmt);
  db->rows_affected_val = (int)mysql_stmt_affected_rows(stmt);

  free(bind);
  mysql_stmt_close(stmt);
  return CORM_OK;
}

/* Map a MYSQL_FIELD type to corm_field_type_t */
static corm_field_type_t
mysql_field_to_corm_type(enum enum_field_types mysql_type) {
  switch (mysql_type) {
  case MYSQL_TYPE_TINY:
  case MYSQL_TYPE_SHORT:
  case MYSQL_TYPE_LONG:
  case MYSQL_TYPE_INT24:
  case MYSQL_TYPE_LONGLONG:
    return CORM_INT64;
  case MYSQL_TYPE_FLOAT:
  case MYSQL_TYPE_DOUBLE:
  case MYSQL_TYPE_DECIMAL:
    return CORM_DOUBLE;
  case MYSQL_TYPE_BLOB:
    return CORM_BLOB;
  default:
    return CORM_TEXT;
  }
}

/* Query using MySQL's simple (non-param) API */
static corm_err_t mysql_simple_query(MYSQL *handle, corm_result_t **out) {
  MYSQL_RES *mysql_res = mysql_store_result(handle);
  if (!mysql_res) {
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

  MYSQL_FIELD *fields = mysql_fetch_fields(mysql_res);
  for (int i = 0; i < col_count; i++) {
    res->column_names[i] = strdup(fields[i].name);
    res->column_types[i] = mysql_field_to_corm_type(fields[i].type);
  }

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

/* Query using MySQL prepared statements (supports parameter binding) */
static corm_err_t mysql_stmt_query(corm_t *db, MYSQL *handle, const char *sql,
                                   corm_value_t *params, int param_count,
                                   corm_result_t **out) {
  MYSQL_STMT *stmt = mysql_stmt_init(handle);
  if (!stmt) {
    snprintf(db->err_msg, sizeof(db->err_msg), "mysql_stmt_init failed");
    return CORM_ERR_NOMEM;
  }

  if (mysql_stmt_prepare(stmt, sql, (unsigned long)strlen(sql)) != 0) {
    snprintf(db->err_msg, sizeof(db->err_msg), "%s", mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    return CORM_ERR_BACKEND;
  }

  if (param_count > 0) {
    MYSQL_BIND *bind = mysql_bind_params(params, param_count);
    if (!bind) {
      mysql_stmt_close(stmt);
      return CORM_ERR_NOMEM;
    }
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
      snprintf(db->err_msg, sizeof(db->err_msg), "%s", mysql_stmt_error(stmt));
      free(bind);
      mysql_stmt_close(stmt);
      return CORM_ERR_BACKEND;
    }
    free(bind);
  }

  if (mysql_stmt_execute(stmt) != 0) {
    snprintf(db->err_msg, sizeof(db->err_msg), "%s", mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    return CORM_ERR_BACKEND;
  }

  MYSQL_RES *meta = mysql_stmt_result_metadata(stmt);
  if (!meta) {
    /* Not a result-returning query */
    *out = NULL;
    mysql_stmt_close(stmt);
    return CORM_OK;
  }

  int col_count = mysql_num_fields(meta);
  MYSQL_FIELD *fields = mysql_fetch_fields(meta);

  /* Store results so max_length is populated */
  if (mysql_stmt_store_result(stmt) != 0) {
    snprintf(db->err_msg, sizeof(db->err_msg), "%s", mysql_stmt_error(stmt));
    mysql_free_result(meta);
    mysql_stmt_close(stmt);
    return CORM_ERR_BACKEND;
  }

  int row_count = (int)mysql_stmt_num_rows(stmt);

  corm_result_t *res = corm_result_new(col_count, row_count);
  if (!res) {
    mysql_free_result(meta);
    mysql_stmt_close(stmt);
    return CORM_ERR_NOMEM;
  }

  /* Column info */
  for (int i = 0; i < col_count; i++) {
    res->column_names[i] = strdup(fields[i].name);
    res->column_types[i] = mysql_field_to_corm_type(fields[i].type);
  }

  if (row_count > 0) {
    /* Set up result bind buffers */
    MYSQL_BIND *result_bind =
        (MYSQL_BIND *)calloc((size_t)col_count, sizeof(MYSQL_BIND));
    my_bool *is_null = (my_bool *)calloc((size_t)col_count, sizeof(my_bool));
    unsigned long *lengths =
        (unsigned long *)calloc((size_t)col_count, sizeof(unsigned long));
    my_bool *errors = (my_bool *)calloc((size_t)col_count, sizeof(my_bool));
    /* Per-column allocated buffer — freed after row copy */
    char **buffers = (char **)calloc((size_t)col_count, sizeof(char *));
    unsigned long *buf_lens =
        (unsigned long *)calloc((size_t)col_count, sizeof(unsigned long));

    if (!result_bind || !is_null || !lengths || !errors || !buffers ||
        !buf_lens) {
      free(result_bind);
      free(is_null);
      free(lengths);
      free(errors);
      for (int i = 0; i < col_count; i++)
        free(buffers[i]);
      free(buffers);
      free(buf_lens);
      mysql_free_result(meta);
      mysql_stmt_close(stmt);
      corm_result_release(res);
      return CORM_ERR_NOMEM;
    }

    for (int i = 0; i < col_count; i++) {
      result_bind[i].is_null = &is_null[i];
      result_bind[i].length = &lengths[i];
      result_bind[i].error = &errors[i];
      /* Map CORM type → MySQL type for result binding */
      switch (res->column_types[i]) {
      case CORM_INT64:
        result_bind[i].buffer_type = MYSQL_TYPE_LONGLONG;
        break;
      case CORM_DOUBLE:
        result_bind[i].buffer_type = MYSQL_TYPE_DOUBLE;
        break;
      case CORM_BLOB:
        result_bind[i].buffer_type = MYSQL_TYPE_BLOB;
        break;
      default:
        result_bind[i].buffer_type = MYSQL_TYPE_STRING;
        break;
      }

      /* Allocate buffer based on column type */
      switch (res->column_types[i]) {
      case CORM_INT64:
        buf_lens[i] = sizeof(long long);
        buffers[i] = (char *)calloc(1, buf_lens[i]);
        result_bind[i].buffer = buffers[i];
        result_bind[i].buffer_length = buf_lens[i];
        break;
      case CORM_DOUBLE:
        buf_lens[i] = sizeof(double);
        buffers[i] = (char *)calloc(1, buf_lens[i]);
        result_bind[i].buffer = buffers[i];
        result_bind[i].buffer_length = buf_lens[i];
        break;
      case CORM_BLOB:
        /* Use max_length from stored result, fallback to 64KB */
        buf_lens[i] =
            (unsigned long)(fields[i].max_length > 0 ? fields[i].max_length
                                                     : 65536);
        buffers[i] = (char *)calloc(1, buf_lens[i]);
        result_bind[i].buffer = buffers[i];
        result_bind[i].buffer_length = buf_lens[i];
        break;
      default:
        /* TEXT / VARCHAR etc */
        buf_lens[i] =
            (unsigned long)(fields[i].max_length > 0 ? fields[i].max_length + 1
                                                     : 4096);
        buffers[i] = (char *)calloc(1, buf_lens[i]);
        result_bind[i].buffer = buffers[i];
        result_bind[i].buffer_length = buf_lens[i];
        break;
      }
    }

    if (mysql_stmt_bind_result(stmt, result_bind) != 0) {
      snprintf(db->err_msg, sizeof(db->err_msg), "%s", mysql_stmt_error(stmt));
      free(result_bind);
      free(is_null);
      free(lengths);
      free(errors);
      for (int i = 0; i < col_count; i++)
        free(buffers[i]);
      free(buffers);
      free(buf_lens);
      mysql_free_result(meta);
      mysql_stmt_close(stmt);
      corm_result_release(res);
      return CORM_ERR_BACKEND;
    }

    /* Fetch rows */
    int r = 0;
    while (mysql_stmt_fetch(stmt) == 0) {
      for (int i = 0; i < col_count; i++) {
        corm_value_t *v = &res->rows[r][i];
        v->type = res->column_types[i];
        if (is_null[i]) {
          v->is_null = true;
          continue;
        }
        switch (v->type) {
        case CORM_INT64:
          v->v.i = *(long long *)buffers[i];
          break;
        case CORM_DOUBLE:
          v->v.f = *(double *)buffers[i];
          break;
        case CORM_TEXT:
          v->v.s = strdup(buffers[i]);
          break;
        case CORM_BLOB:
          v->v.blob.data = malloc(lengths[i]);
          if (v->v.blob.data) {
            memcpy(v->v.blob.data, buffers[i], lengths[i]);
            v->v.blob.len = (size_t)lengths[i];
          }
          break;
        default:
          v->v.s = strdup(buffers[i]);
          v->type = CORM_TEXT;
          break;
        }
      }
      r++;
    }

    free(result_bind);
    free(is_null);
    free(lengths);
    free(errors);
    for (int i = 0; i < col_count; i++)
      free(buffers[i]);
    free(buffers);
    free(buf_lens);
  }

  mysql_free_result(meta);
  mysql_stmt_close(stmt);
  *out = res;
  return CORM_OK;
}

static corm_err_t corm_mysql_query(corm_t *db, const char *sql,
                                   corm_value_t *params, int param_count,
                                   corm_result_t **out) {
  MYSQL *handle = (MYSQL *)db->conn;

  if (param_count == 0) {
    if (mysql_query(handle, sql) != 0) {
      snprintf(db->err_msg, sizeof(db->err_msg), "%s", mysql_error(handle));
      return CORM_ERR_BACKEND;
    }
    return mysql_simple_query(handle, out);
  }

  return mysql_stmt_query(db, handle, sql, params, param_count, out);
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
