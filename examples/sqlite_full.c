#include "../src/corm_pub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Models ── */

typedef struct {
  int id;
  char name[64];
  int age;
  float score;
} user_t;

static corm_field_t user_fields[] = {
    CORM_FIELD(user_t, id, CORM_INT, CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC,
               NULL),
    CORM_FIELD(user_t, name, CORM_STRING, CORM_FLAG_NOT_NULL, NULL),
    CORM_FIELD(user_t, age, CORM_INT, 0, "0"),
    CORM_FIELD(user_t, score, CORM_FLOAT, 0, "0.0"),
};

static corm_model_t user_model = {
    .table_name = "users",
    .struct_size = sizeof(user_t),
    .fields = user_fields,
    .field_count = 4,
    .primary_key = &user_fields[0],
};

typedef struct {
  int id;
  char title[128];
  char deleted_at[32]; /* soft-delete timestamp */
} article_t;

static corm_field_t article_fields[] = {
    CORM_FIELD(article_t, id, CORM_INT, CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC,
               NULL),
    CORM_FIELD(article_t, title, CORM_STRING, CORM_FLAG_NOT_NULL, NULL),
    CORM_FIELD(article_t, deleted_at, CORM_STRING, CORM_FLAG_SOFT_DELETE, ""),
};

static corm_model_t article_model = {
    .table_name = "articles",
    .struct_size = sizeof(article_t),
    .fields = article_fields,
    .field_count = 3,
    .primary_key = &article_fields[0],
};

/* ── Helpers ── */

static void print_separator(const char *title) {
  printf("\n");
  printf("── %s ─────────────────────────────────────\n", title);
}

/* ── Main ── */

int main(void) {
  corm_t *db = NULL;
  corm_err_t err;

  printf("╔══════════════════════════════════╗\n");
  printf("║   CORM — SQLite Full Example     ║\n");
  printf("╚══════════════════════════════════╝\n");

  /* ── 1. Open database (file-based) ── */
  printf("\n1. Opening file-based SQLite database...\n");
  err = corm_open("sqlite3:///tmp/corm_example.db", &db);
  if (err != CORM_OK) {
    printf("   ERROR: %d\n", err);
    return 1;
  }
  printf("   ✓ Connected to /tmp/corm_example.db\n");

  /* ── 2. Register models ── */
  corm_register_model(db, &user_model);
  corm_register_model(db, &article_model);
  printf("   ✓ Registered 2 models\n");

  /* ── 3. Auto-migrate ── */
  corm_model_t *models[] = {&user_model, &article_model};
  err = corm_auto_migrate(db, models, 2);
  printf("   ✓ Tables created/verified\n");

  /* ── 4. Insert via ORM ── */
  print_separator("Insert via ORM");

  user_t users[] = {
      {0, "Alice", 30, 95.5f},
      {0, "Bob", 25, 87.0f},
      {0, "Charlie", 35, 92.3f},
      {0, "Diana", 28, 88.1f},
  };
  for (int i = 0; i < 4; i++) {
    corm_create_one(db, &user_model, &users[i], NULL);
  }
  printf("   Inserted %d users\n", 4);

  /* ── 5. Find one ── */
  print_separator("corm_find_one");
  user_t found;
  err = corm_find_one(db, &user_model, "id = 1", &found);
  if (err == CORM_OK) {
    printf("   Found: [%d] %s, Age: %d, Score: %.1f\n", found.id, found.name,
           found.age, found.score);
  }

  /* ── 6. Count ── */
  print_separator("corm_count");
  int cnt = 0;
  corm_count(db, &user_model, NULL, &cnt);
  printf("   Total users: %d\n", cnt);

  /* ── 7. Find all ── */
  print_separator("corm_find_all");
  user_t all_users[10];
  int total = 0;
  corm_find_all(db, &user_model, NULL, all_users, &total);
  for (int i = 0; i < total; i++) {
    printf("   [%d] %s — Age: %d, Score: %.1f\n", all_users[i].id,
           all_users[i].name, all_users[i].age, all_users[i].score);
  }

  /* ── 8. Batch insert ── */
  print_separator("Batch insert (corm_create_batch)");
  user_t batch[3] = {
      {0, "Eve", 22, 91.0f}, {0, "Frank", 40, 79.5f}, {0, "Grace", 33, 85.2f}};
  int inserted = 0;
  corm_create_batch(db, &user_model, batch, 3, 100, &inserted);
  printf("   Batch inserted %d users\n", inserted);

  /* ── 9. Batch update ── */
  print_separator("Batch update (corm_update_batch)");
  /* Update Bob's score */
  batch[0].id = 2;
  batch[0].score = 99.9f;
  int affected = 0;
  corm_update_batch(db, &user_model, batch, 1, &affected);
  printf("   Updated %d record(s)\n", affected);

  /* ── 10. Raw query ── */
  print_separator("Raw query (corm_raw)");
  corm_result_t *res = NULL;
  corm_raw(db, "SELECT * FROM users ORDER BY score DESC", &res);
  if (res) {
    corm_result_reset(res);
    while (corm_result_next(res)) {
      const char *name = corm_result_string(res, res->current_row, 1);
      double score = corm_result_double(res, res->current_row, 3);
      printf("   %-8s → %.1f\n", name ? name : "?", score);
    }
    corm_result_release(res);
  }

  /* ── 11. Soft delete ── */
  print_separator("Soft delete (CORM_FLAG_SOFT_DELETE)");
  article_t articles[] = {
      {0, "Introduction to C", ""},
      {0, "Advanced Memory Management", ""},
  };
  /* Explicitly set soft-delete field to NULL (not empty string) */
  memset(articles[0].deleted_at, 0, sizeof(articles[0].deleted_at));
  memset(articles[1].deleted_at, 0, sizeof(articles[1].deleted_at));
  for (int i = 0; i < 2; i++) {
    corm_create_one(db, &article_model, &articles[i], NULL);
  }
  printf("   Created %d articles\n", 2);

  /* Delete first article — triggers soft delete UPDATE */
  corm_query_t *q = corm_query_new(db, &article_model);
  /* Use literal value in WHERE (no params needed) */
  corm_query_where(q, "id = 1");
  int aff = 0;
  corm_delete(q, &aff);
  corm_query_free(q);
  printf("   Soft-deleted article 1 (%d row(s) affected)\n", aff);

  /* Verify soft delete */
  corm_raw(db, "SELECT id, title, deleted_at FROM articles", &res);
  if (res) {
    corm_result_reset(res);
    while (corm_result_next(res)) {
      int id = (int)corm_result_int(res, res->current_row, 0);
      const char *title = corm_result_string(res, res->current_row, 1);
      const char *del = corm_result_string(res, res->current_row, 2);
      printf("   [%d] %s — deleted_at=%s\n", id, title ? title : "?",
             del && del[0] ? del : "(none)");
    }
    corm_result_release(res);
  }

  /* ── 12. Transaction ── */
  print_separator("Transaction");
  corm_begin(db);
  corm_exec(db,
            "INSERT INTO users (name, age, score) VALUES ('Heidi', 27, 90.0)");
  corm_exec(db,
            "INSERT INTO users (name, age, score) VALUES ('Ivan', 31, 82.0)");
  corm_commit(db);
  printf("   Transaction committed: 2 users added\n");

  /* ── 13. Exec params ── */
  print_separator("Parameterized exec (corm_exec_params)");
  corm_value_t val = {.type = CORM_STRING, .is_null = false, .v.s = "Judy"};
  corm_exec_params(db, "INSERT INTO users (name) VALUES (?)", &val, 1);
  printf("   Inserted 'Judy' via parameterized SQL\n");

  /* ── 14. Isolation level ── */
  print_separator("Transaction isolation level");
  corm_set_isolation(db, CORM_ISOLATION_READ_COMMITTED);
  printf("   Set isolation level to READ COMMITTED\n");

  /* ── 15. Final count ── */
  print_separator("Final stats");
  corm_count(db, &user_model, NULL, &cnt);
  printf("   Total users: %d\n", cnt);

  /* Use raw SQL for article count (corm_count filters soft-deleted) */
  corm_raw(db, "SELECT COUNT(*) FROM articles", &res);
  if (res && corm_result_next(res)) {
    printf("   Total articles (raw): %lld\n",
           (long long)corm_result_int(res, 0, 0));
  }
  corm_result_release(res);

  /* ── Cleanup ── */
  corm_close(db);
  printf("\n   Database saved to /tmp/corm_example.db\n");
  printf("\n╔══════════════════════════════════╗\n");
  printf("║   Example completed successfully ║\n");
  printf("╚══════════════════════════════════╝\n");
  return 0;
}
