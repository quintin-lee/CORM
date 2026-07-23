#include "corm_pub.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void test_where_in_and_between(void) {
  corm_query_t q;
  memset(&q, 0, sizeof(q));
  corm_strbuf_init(&q.where);

  corm_value_t vals[3];
  vals[0].type = CORM_INT;
  vals[0].v.i = 10;
  vals[1].type = CORM_INT;
  vals[1].v.i = 20;
  vals[2].type = CORM_INT;
  vals[2].v.i = 30;

  corm_query_where_in(&q, "age", vals, 3);
  assert(strstr(corm_strbuf_cstr(&q.where), "age IN (?, ?, ?)") != NULL);
  assert(q.param_count == 3);

  corm_value_t min_val = {.type = CORM_INT, .v.i = 1};
  corm_value_t max_val = {.type = CORM_INT, .v.i = 100};
  corm_query_where_between(&q, "score", min_val, max_val);
  assert(strstr(corm_strbuf_cstr(&q.where), "score BETWEEN ? AND ?") != NULL);
  assert(q.param_count == 5);

  corm_strbuf_free(&q.where);
  if (q.params)
    free(q.params);
  printf("test_where_in_and_between PASSED\n");
}

int main(void) {
  test_where_in_and_between();
  return 0;
}
