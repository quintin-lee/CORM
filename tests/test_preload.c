#include "corm/corm.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    int id;
    char title[64];
    int user_id;
} Order;

static corm_field_t order_fields[] = {
    CORM_FIELD(Order, id, CORM_INT, CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC, NULL),
    CORM_FIELD(Order, title, CORM_STRING, 0, NULL),
    CORM_FIELD(Order, user_id, CORM_INT, 0, NULL),
};

static corm_model_t order_model = {
    .table_name = "preload_orders",
    .struct_size = sizeof(Order),
    .fields = order_fields,
    .field_count = 3,
    .primary_key = &order_fields[0],
};

typedef struct {
    int id;
    char name[64];
} PreloadUser;

static corm_field_t user_fields[] = {
    CORM_FIELD(PreloadUser, id, CORM_INT, CORM_FLAG_PRIMARY | CORM_FLAG_AUTOINC, NULL),
    CORM_FIELD(PreloadUser, name, CORM_STRING, 0, NULL),
};

static corm_model_t user_model = {
    .table_name = "preload_users",
    .struct_size = sizeof(PreloadUser),
    .fields = user_fields,
    .field_count = 2,
    .primary_key = &user_fields[0],
};

void test_relation_preload(void) {
    corm_t *db;
    corm_open("sqlite3://:memory:", &db);
    corm_register_model(db, &user_model);
    corm_register_model(db, &order_model);

    corm_model_t *models[] = { &user_model, &order_model };
    corm_auto_migrate(db, models, 2);

    PreloadUser u = { .name = "Alice" };
    int64_t uid = 0;
    corm_create_one(db, &user_model, &u, &uid);

    Order o1 = { .title = "Book", .user_id = (int)uid };
    Order o2 = { .title = "Pen", .user_id = (int)uid };
    corm_create_one(db, &order_model, &o1, NULL);
    corm_create_one(db, &order_model, &o2, NULL);

    corm_query_t *q = corm_query_new(db, &user_model);
    corm_query_preload(q, "preload_orders");
    corm_result_t *res = NULL;
    corm_err_t err = corm_find(q, &res);
    assert(err == CORM_OK);
    assert(res->row_count == 1);

    corm_result_release(res);
    corm_query_free(q);
    corm_close(db);
    printf("test_relation_preload PASSED\n");
}

int main(void) {
    test_relation_preload();
    return 0;
}
