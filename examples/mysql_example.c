#include "../src/corm_pub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Define a model ── */
typedef struct {
  int id;
  char name[256];
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

int main(void) {
  corm_t *db = NULL;
  corm_err_t err;

  printf("╔══════════════════════════════════╗\n");
  printf("║   CORM — MySQL Usage Example     ║\n");
  printf("╚══════════════════════════════════╝\n\n");

  /* ── Open database ── */
  printf("1. Connecting to MySQL...\n");
  err = corm_open("mysql://root:Test123!@127.0.0.1:3306/test_db", &db);
  if (err != CORM_OK) {
    printf("   ERROR: Could not connect to MySQL (code %d)\n", err);
    printf("   (Make sure MySQL is running and credentials are correct)\n");
    return 1;
  }
  printf("   ✓ Connected to MySQL\n\n");

  /* ── Register model ── */
  printf("2. Registering model...\n");
  corm_register_model(db, &user_model);
  printf("   ✓ Model 'users' registered\n\n");

  /* ── Drop existing table for clean start ── */
  printf("3. Cleaning up old data...\n");
  corm_exec(db, "DROP TABLE IF EXISTS users");
  printf("   ✓ Old table dropped\n\n");

  /* ── Auto-migrate ── */
  printf("4. Running auto-migration...\n");
  corm_model_t *models[] = {&user_model};
  err = corm_auto_migrate(db, models, 1);
  if (err == CORM_OK)
    printf("   ✓ Table 'users' created\n");
  else
    printf("   ⚠ Migration returned %d (table may already exist)\n", err);
  printf("\n");

  /* ── Insert using ORM ── */
  printf("5. Inserting records (ORM)...\n");
  user_t users_to_insert[] = {{0, "Alice", 30, 95.5f},
                              {0, "Bob", 25, 87.0f},
                              {0, "Charlie", 35, 92.3f}};
  for (int i = 0; i < 3; i++) {
    err = corm_create_one(db, &user_model, &users_to_insert[i], NULL);
    printf("   %s\n", err == CORM_OK ? "✓ Record inserted" : "✗ Failed");
  }
  printf("\n");

  /* ── Query using raw SQL ── */
  printf("6. Querying records (raw SQL)...\n");
  corm_result_t *res = NULL;
  err = corm_raw(db, "SELECT * FROM users ORDER BY score DESC", &res);
  if (err == CORM_OK && res) {
    printf("   Found %d record(s):\n", res->row_count);
    corm_result_reset(res);
    int row_num = 1;
    while (corm_result_next(res)) {
      int id = (int)corm_result_int(res, res->current_row, 0);
      const char *name = corm_result_string(res, res->current_row, 1);
      int age = (int)corm_result_int(res, res->current_row, 2);
      double score = corm_result_double(res, res->current_row, 3);
      printf("   %d. [%d] %s — Age: %d, Score: %.1f\n", row_num++, id,
             name ? name : "?", age, score);
    }
    corm_result_release(res);
  } else {
    printf("   ⚠ Query returned code %d\n", err);
  }
  printf("\n");

  /* ── Transaction example ── */
  printf("7. Transaction example...\n");
  err = corm_begin(db);
  if (err == CORM_OK)
    printf("   ✓ BEGIN\n");

  err = corm_exec(
      db, "INSERT INTO users (name, age, score) VALUES ('Dave', 28, 78.5)");
  if (err == CORM_OK)
    printf("   ✓ Dave inserted\n");

  err = corm_commit(db);
  if (err == CORM_OK)
    printf("   ✓ COMMIT\n");
  printf("\n");

  /* ── Final count ── */
  res = NULL;
  corm_raw(db, "SELECT COUNT(*) FROM users", &res);
  if (res) {
    printf("8. Total users in database: ");
    corm_result_reset(res);
    if (corm_result_next(res)) {
      printf("%lld\n", (long long)corm_result_int(res, 0, 0));
    }
    corm_result_release(res);
  }

  /* ── Close ── */
  printf("\n9. Closing database...\n");
  err = corm_close(db);
  printf("   %s\n", err == CORM_OK ? "✓ Done" : "✗ Error closing");

  printf("\n╔══════════════════════════════════╗\n");
  printf("║   CORM MySQL example completed.    ║\n");
  printf("╚══════════════════════════════════╝\n");
  return 0;
}
