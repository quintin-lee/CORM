#ifndef CROM_STRBUF_H
#define CROM_STRBUF_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Dynamic string buffer */
typedef struct {
  char *buf;
  size_t len; /* length of string (excluding NUL) */
  size_t cap; /* allocated capacity */
} corm_strbuf_t;

#define CORM_STRBUF_INIT {.buf = NULL, .len = 0, .cap = 0}

static inline corm_err_t corm_strbuf_init(corm_strbuf_t *sb) {
  sb->buf = NULL;
  sb->len = 0;
  sb->cap = 0;
  return CORM_OK;
}

static inline corm_err_t corm_strbuf_grow(corm_strbuf_t *sb, size_t extra) {
  if (sb->len + extra + 1 <= sb->cap)
    return CORM_OK;
  size_t new_cap = sb->cap ? sb->cap * 2 : 64;
  while (new_cap < sb->len + extra + 1)
    new_cap *= 2;
  char *tmp = realloc(sb->buf, new_cap);
  if (!tmp)
    return CORM_ERR_NOMEM;
  sb->buf = tmp;
  sb->cap = new_cap;
  return CORM_OK;
}

static inline corm_err_t corm_strbuf_append(corm_strbuf_t *sb, const char *s) {
  size_t slen = strlen(s);
  corm_err_t err = corm_strbuf_grow(sb, slen);
  if (err)
    return err;
  memcpy(sb->buf + sb->len, s, slen);
  sb->len += slen;
  sb->buf[sb->len] = '\0';
  return CORM_OK;
}

static inline corm_err_t corm_strbuf_appendn(corm_strbuf_t *sb, const char *s,
                                             size_t n) {
  corm_err_t err = corm_strbuf_grow(sb, n);
  if (err)
    return err;
  memcpy(sb->buf + sb->len, s, n);
  sb->len += n;
  sb->buf[sb->len] = '\0';
  return CORM_OK;
}

static inline corm_err_t corm_strbuf_appendf(corm_strbuf_t *sb, const char *fmt,
                                             ...) {
  va_list ap;
  va_start(ap, fmt);
  int needed = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (needed < 0)
    return CORM_ERR_GENERIC;

  corm_err_t err = corm_strbuf_grow(sb, (size_t)needed);
  if (err)
    return err;

  va_start(ap, fmt);
  vsnprintf(sb->buf + sb->len, (size_t)(needed + 1), fmt, ap);
  va_end(ap);
  sb->len += (size_t)needed;
  return CORM_OK;
}

static inline void corm_strbuf_clear(corm_strbuf_t *sb) {
  sb->len = 0;
  if (sb->buf)
    sb->buf[0] = '\0';
}

static inline void corm_strbuf_free(corm_strbuf_t *sb) {
  free(sb->buf);
  sb->buf = NULL;
  sb->len = 0;
  sb->cap = 0;
}

static inline const char *corm_strbuf_cstr(corm_strbuf_t *sb) {
  return sb->buf ? sb->buf : "";
}

#ifdef __cplusplus
}
#endif

#endif /* CROM_STRBUF_H */
