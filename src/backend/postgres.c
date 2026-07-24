#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CORM_HAVE_POSTGRES
#include <libpq-fe.h>
#endif

#include "corm_pub.h"

#include "../internal/corm_internal.h"

/* ── PostgreSQL backend ── */

#ifdef CORM_HAVE_POSTGRES

static corm_err_t pg_open(corm_t *db, const char *dsn) {
  /* PostgreSQL DSN: host=xxx port=xxx dbname=xxx user=xxx password=xxx
   * The user passes postgres://user:pass@host:port/dbname
   * We convert to key=value format for PQconnectdb
   */
  char conninfo[1024] = "";
  char *p = conninfo;

  /* Parse the URI-like DSN and convert to key=value */
  /* postgres://user:pass@host:port/dbname?sslmode=require */
  const char *s = dsn;
  char user[256] = "";
  char pass[256] = "";
  char host[256] = "localhost";
  int port = 5432;
  char dbname[256] = "";

  /* Check for user:pass@host pattern */
  char buf[1024];
  strncpy(buf, dsn, sizeof(buf) - 1);

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
    s = at + 1;
  } else {
    s = buf;
  }

  /* Parse host:port/dbname */
  char *slash = strchr(s, '/');
  if (slash) {
    strncpy(dbname, slash + 1, sizeof(dbname) - 1);
    size_t hostlen = (size_t)(slash - s);
    if (hostlen > 0 && hostlen < sizeof(host)) {
      strncpy(host, s, hostlen);
      host[hostlen] = '\0';
    }
    char *hcolon = strchr(host, ':');
    if (hcolon) {
      *hcolon = '\0';
      port = atoi(hcolon + 1);
    }
  } else {
    char *hcolon = strchr(s, ':');
    if (hcolon) {
      size_t hostlen = (size_t)(hcolon - s);
      strncpy(host, s, hostlen);
      host[hostlen] = '\0';
      port = atoi(hcolon + 1);
    } else {
      strncpy(host, s, sizeof(host) - 1);
    }
  }

  /* Build conninfo string */
  p += snprintf(p, sizeof(conninfo) - (size_t)(p - conninfo), "host=%s port=%d",
                host, port);
  if (dbname[0])
    p += snprintf(p, sizeof(conninfo) - (size_t)(p - conninfo), " dbname=%s",
                  dbname);
  if (user[0])
    p += snprintf(p, sizeof(conninfo) - (size_t)(p - conninfo), " user=%s",
                  user);
  if (pass[0])
    p += snprintf(p, sizeof(conninfo) - (size_t)(p - conninfo), " password=%s",
                  pass);

  PGconn *handle = PQconnectdb(conninfo);
  if (PQstatus(handle) != CONNECTION_OK) {
    corm_set_err_msg(db, "%s", PQerrorMessage(handle));
    PQfinish(handle);
    return CORM_ERR_BACKEND;
  }

  db->conn = handle;
  return CORM_OK;
}

static corm_err_t pg_close(corm_t *db) {
  PGconn *handle = (PGconn *)db->conn;
  if (handle) {
    PQfinish(handle);
    db->conn = NULL;
  }
  return CORM_OK;
}

static corm_err_t pg_ping(corm_t *db) {
  PGconn *handle = (PGconn *)db->conn;
  if (!handle)
    return CORM_ERR_BACKEND;
  if (PQstatus(handle) != CONNECTION_OK)
    return CORM_ERR_BACKEND;
  return CORM_OK;
}

static corm_err_t pg_exec(corm_t *db, const char *sql, corm_value_t *params,
                          int param_count) {
  PGconn *handle = (PGconn *)db->conn;

  if (param_count > 0) {
    /* Parameterized execution */
    const char *const *param_values =
        (const char *const *)malloc((size_t)param_count * sizeof(char *));
    int *param_lengths = (int *)malloc((size_t)param_count * sizeof(int));
    int *param_formats = (int *)malloc((size_t)param_count * sizeof(int));
    char **tmp_strs = (char **)calloc((size_t)param_count, sizeof(char *));

    if (!param_values || !param_lengths || !param_formats || !tmp_strs) {
      free((void *)param_values);
      free(param_lengths);
      free(param_formats);
      for (int i = 0; i < param_count; i++)
        free(tmp_strs[i]);
      free(tmp_strs);
      return CORM_ERR_NOMEM;
    }

    /* Build param arrays */
    for (int i = 0; i < param_count; i++) {
      param_formats[i] = 0; /* text format */
      if (params[i].is_null) {
        param_values[i] = NULL;
        param_lengths[i] = 0;
      } else {
        switch (params[i].type) {
        case CORM_INT:
        case CORM_INT64: {
          char buf[32];
          snprintf(buf, sizeof(buf), "%" PRId64, params[i].v.i);
          tmp_strs[i] = strdup(buf);
          param_values[i] = tmp_strs[i];
          param_lengths[i] = (int)strlen(tmp_strs[i]);
          break;
        }
        case CORM_FLOAT:
        case CORM_DOUBLE: {
          char buf[64];
          snprintf(buf, sizeof(buf), "%.15g", params[i].v.f);
          tmp_strs[i] = strdup(buf);
          param_values[i] = tmp_strs[i];
          param_lengths[i] = (int)strlen(tmp_strs[i]);
          break;
        }
        case CORM_STRING:
        case CORM_TEXT:
          param_values[i] = params[i].v.s;
          param_lengths[i] = (int)strlen(params[i].v.s);
          break;
        case CORM_BOOL:
          param_values[i] = params[i].v.b ? "t" : "f";
          param_lengths[i] = 1;
          break;
        case CORM_BLOB:
          param_formats[i] = 1; /* binary */
          param_values[i] = (const char *)params[i].v.blob.data;
          param_lengths[i] = (int)params[i].v.blob.len;
          break;
        }
      }
    }

    PGresult *pgres = PQexecParams(handle, sql, param_count, NULL, param_values,
                                   param_lengths, param_formats, 0);

    free((void *)param_values);
    free(param_lengths);
    free(param_formats);
    for (int i = 0; i < param_count; i++)
      free(tmp_strs[i]);
    free(tmp_strs);

    ExecStatusType status = PQresultStatus(pgres);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
      corm_set_err_msg(db, "%s", PQerrorMessage(handle));
      PQclear(pgres);
      return CORM_ERR_BACKEND;
    }
    db->rows_affected_val = PQcmdTuples(pgres) ? atoi(PQcmdTuples(pgres)) : 0;
    if (db->rows_affected_val > 0) {
      /* For INSERT, try to get last insert ID from the result */
      if (PQntuples(pgres) == 1 && PQnfields(pgres) >= 1) {
        char *oid_str = PQgetvalue(pgres, 0, 0);
        if (oid_str && oid_str[0]) {
          db->last_insert_id_val = strtoll(oid_str, NULL, 10);
        }
      }
    }
    PQclear(pgres);
  } else {
    PGresult *pgres = PQexec(handle, sql);
    ExecStatusType status = PQresultStatus(pgres);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
      corm_set_err_msg(db, "%s", PQerrorMessage(handle));
      PQclear(pgres);
      return CORM_ERR_BACKEND;
    }
    db->rows_affected_val = PQcmdTuples(pgres) ? atoi(PQcmdTuples(pgres)) : 0;
    if (db->rows_affected_val > 0) {
      if (PQntuples(pgres) == 1 && PQnfields(pgres) >= 1) {
        char *oid_str = PQgetvalue(pgres, 0, 0);
        if (oid_str && oid_str[0]) {
          db->last_insert_id_val = strtoll(oid_str, NULL, 10);
        }
      }
    }
    PQclear(pgres);
  }

  return CORM_OK;
}

static corm_err_t pg_query(corm_t *db, const char *sql, corm_value_t *params,
                           int param_count, corm_result_t **out) {
  PGconn *handle = (PGconn *)db->conn;
  PGresult *pgres;

  if (param_count > 0) {
    const char *const *param_values =
        (const char *const *)malloc((size_t)param_count * sizeof(char *));
    int *param_lengths = (int *)malloc((size_t)param_count * sizeof(int));
    int *param_formats = (int *)malloc((size_t)param_count * sizeof(int));
    char **tmp_strs = (char **)calloc((size_t)param_count, sizeof(char *));

    if (!param_values || !param_lengths || !param_formats || !tmp_strs) {
      free((void *)param_values);
      free(param_lengths);
      free(param_formats);
      for (int i = 0; i < param_count; i++)
        free(tmp_strs[i]);
      free(tmp_strs);
      return CORM_ERR_NOMEM;
    }

    for (int i = 0; i < param_count; i++) {
      param_formats[i] = 0;
      if (params[i].is_null) {
        param_values[i] = NULL;
        param_lengths[i] = 0;
      } else {
        switch (params[i].type) {
        case CORM_INT:
        case CORM_INT64: {
          tmp_strs[i] = malloc(sizeof(int64_t));
          memcpy(tmp_strs[i], &params[i].v.i, sizeof(int64_t));
          param_values[i] = tmp_strs[i];
          param_lengths[i] = sizeof(int64_t);
          param_formats[i] = 1; /* binary */
          break;
        }
        case CORM_FLOAT:
        case CORM_DOUBLE: {
          tmp_strs[i] = malloc(sizeof(double));
          memcpy(tmp_strs[i], &params[i].v.f, sizeof(double));
          param_values[i] = tmp_strs[i];
          param_lengths[i] = sizeof(double);
          param_formats[i] = 1; /* binary */
          break;
        }
        case CORM_STRING:
        case CORM_TEXT:
          param_values[i] = params[i].v.s;
          param_lengths[i] = (int)strlen(params[i].v.s);
          break;
        case CORM_BOOL:
          param_values[i] = params[i].v.b ? "t" : "f";
          param_lengths[i] = 1;
          break;
        case CORM_BLOB:
          param_formats[i] = 1;
          param_values[i] = (const char *)params[i].v.blob.data;
          param_lengths[i] = (int)params[i].v.blob.len;
          break;
        }
      }
    }

    pgres = PQexecParams(handle, sql, param_count, NULL, param_values,
                         param_lengths, param_formats, 0);

    free((void *)param_values);
    free(param_lengths);
    free(param_formats);
    for (int i = 0; i < param_count; i++)
      free(tmp_strs[i]);
    free(tmp_strs);
  } else {
    pgres = PQexec(handle, sql);
  }

  if (!pgres || (PQresultStatus(pgres) != PGRES_TUPLES_OK &&
                 PQresultStatus(pgres) != PGRES_COMMAND_OK)) {
    corm_set_err_msg(db, "%s", PQerrorMessage(handle));
    PQclear(pgres);
    return CORM_ERR_BACKEND;
  }

  int col_count = PQnfields(pgres);
  int row_count = PQntuples(pgres);

  corm_result_t *res = corm_result_new(col_count, row_count);
  if (!res) {
    PQclear(pgres);
    return CORM_ERR_NOMEM;
  }

  /* Column info */
  for (int i = 0; i < col_count; i++) {
    res->column_names[i] = strdup(PQfname(pgres, i));
    Oid pgtype = PQftype(pgres, i);
    switch (pgtype) {
    case 20:
    case 21:
    case 23:
    case 26: /* int8, int2, int4, oid */
      res->column_types[i] = CORM_INT64;
      break;
    case 700:
    case 701: /* float4, float8 */
      res->column_types[i] = CORM_DOUBLE;
      break;
    case 16: /* bool */
      res->column_types[i] = CORM_BOOL;
      break;
    case 17: /* bytea */
      res->column_types[i] = CORM_BLOB;
      break;
    default:
      res->column_types[i] = CORM_TEXT;
      break;
    }
  }

  /* Rows */
  for (int r = 0; r < row_count; r++) {
    for (int i = 0; i < col_count; i++) {
      corm_value_t *v = &res->rows[r][i];
      v->type = res->column_types[i];
      if (PQgetisnull(pgres, r, i)) {
        v->is_null = true;
        continue;
      }
      switch (v->type) {
      case CORM_INT64:
        v->v.i = strtoll(PQgetvalue(pgres, r, i), NULL, 10);
        break;
      case CORM_DOUBLE:
        v->v.f = strtod(PQgetvalue(pgres, r, i), NULL);
        break;
      case CORM_BOOL:
        v->v.b = (PQgetvalue(pgres, r, i)[0] == 't');
        break;
      case CORM_BLOB: {
        size_t len;
        unsigned char *blob =
            PQunescapeBytea((unsigned char *)PQgetvalue(pgres, r, i), &len);
        v->v.blob.data = malloc(len);
        if (v->v.blob.data) {
          memcpy(v->v.blob.data, blob, len);
          v->v.blob.len = len;
        }
        PQfreemem(blob);
        break;
      }
      default:
        v->v.s = strdup(PQgetvalue(pgres, r, i));
        break;
      }
    }
  }

  PQclear(pgres);
  *out = res;
  return CORM_OK;
}

static corm_err_t pg_begin(corm_t *db) { return pg_exec(db, "BEGIN", NULL, 0); }

static corm_err_t pg_commit(corm_t *db) {
  return pg_exec(db, "COMMIT", NULL, 0);
}

static corm_err_t pg_rollback(corm_t *db) {
  return pg_exec(db, "ROLLBACK", NULL, 0);
}

static size_t pg_escape(corm_t *db, char *dst, const char *src, size_t len) {
  PGconn *handle = (PGconn *)db->conn;
  if (!handle)
    return len * 2 + 1;
  return (size_t)PQescapeStringConn(handle, dst, src, len, NULL);
}

static int64_t pg_last_id(corm_t *db) { return db->last_insert_id_val; }

static corm_err_t pg_describe_table(corm_t *db, const char *table_name,
                                    corm_column_info_t **out, int *count) {
  PGconn *handle = (PGconn *)db->conn;
  if (!handle)
    return CORM_ERR_BACKEND;

  const char *sql = "SELECT c.column_name, c.data_type, c.is_nullable,"
                    "  CASE WHEN pk.col IS NOT NULL THEN 1 ELSE 0 END,"
                    "  c.column_default "
                    "FROM information_schema.columns c "
                    "LEFT JOIN ("
                    "  SELECT ku.column_name AS col"
                    "  FROM information_schema.table_constraints tc"
                    "  JOIN information_schema.key_column_usage ku"
                    "    ON tc.constraint_name = ku.constraint_name"
                    "  WHERE tc.table_name = $1"
                    "    AND tc.constraint_type = 'PRIMARY KEY'"
                    ") pk ON c.column_name = pk.col "
                    "WHERE c.table_name = $1 "
                    "ORDER BY c.ordinal_position";

  const char *param_values[1] = {table_name};
  PGresult *pgres =
      PQexecParams(handle, sql, 1, NULL, param_values, NULL, NULL, 0);

  if (!pgres || PQresultStatus(pgres) != PGRES_TUPLES_OK) {
    if (pgres)
      PQclear(pgres);
    corm_set_err_msg(db, "%s", PQerrorMessage(handle));
    return CORM_ERR_BACKEND;
  }

  int n = PQntuples(pgres);
  *out = (corm_column_info_t *)calloc((size_t)(n > 0 ? n : 0),
                                      sizeof(corm_column_info_t));
  if (!*out && n > 0) {
    PQclear(pgres);
    return CORM_ERR_NOMEM;
  }

  for (int i = 0; i < n; i++) {
    corm_column_info_t *col = &(*out)[i];
    col->name = strdup(PQgetvalue(pgres, i, 0));
    col->type_name = strdup(PQgetvalue(pgres, i, 1));
    col->not_null = (strcmp(PQgetvalue(pgres, i, 2), "NO") == 0) ? 1 : 0;
    col->is_pk = (PQgetvalue(pgres, i, 3)[0] == '1') ? 1 : 0;
    const char *def = PQgetvalue(pgres, i, 4);
    col->default_value = (def && def[0] != '\0') ? strdup(def) : NULL;
  }

  PQclear(pgres);
  *count = n;
  return CORM_OK;
}

static int pg_affected(corm_t *db) {
  PGconn *handle = (PGconn *)db->conn;
  if (!handle)
    return 0;
  return db->rows_affected_val;
}

static corm_backend_t postgres_backend = {
    .name = "postgres",
    .type = CORM_BACKEND_POSTGRES,
    .open = pg_open,
    .close = pg_close,
    .ping = pg_ping,
    .exec = pg_exec,
    .query = pg_query,
    .begin = pg_begin,
    .commit = pg_commit,
    .rollback = pg_rollback,
    .escape_string = pg_escape,
    .last_insert_id = pg_last_id,
    .rows_affected = pg_affected,
    .describe_table = pg_describe_table,
};

__attribute__((constructor)) static void pg_register(void) {
  corm_register_backend(CORM_BACKEND_POSTGRES, &postgres_backend);
}

corm_err_t corm_register_postgres_backend(void) {
  return corm_register_backend(CORM_BACKEND_POSTGRES, &postgres_backend);
}

#else /* CORM_HAVE_POSTGRES */

static corm_backend_t postgres_stub_backend = {
    .name = "postgres (unavailable)",
    .type = CORM_BACKEND_POSTGRES,
};

__attribute__((constructor)) static void pg_stub_register(void) {
  corm_register_backend(CORM_BACKEND_POSTGRES, &postgres_stub_backend);
}

corm_err_t corm_register_postgres_backend(void) {
  return corm_register_backend(CORM_BACKEND_POSTGRES, &postgres_stub_backend);
}

#endif /* CORM_HAVE_POSTGRES */
