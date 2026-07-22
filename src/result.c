#include <stdlib.h>
#include <string.h>
#include "corm_pub.h"

/* ── Result set ── */

corm_result_t *corm_result_new(int column_count, int row_count) {
    corm_result_t *r = (corm_result_t *)calloc(1, sizeof(corm_result_t));
    if (!r) return NULL;

    r->column_count = column_count;
    r->row_count = row_count;
    r->current_row = -1;
    r->refcount = 1;

    r->column_names = (char **)calloc((size_t)column_count, sizeof(char *));
    if (!r->column_names) { free(r); return NULL; }

    r->column_types = (corm_field_type_t *)calloc((size_t)column_count, sizeof(corm_field_type_t));
    if (!r->column_types) { free(r->column_names); free(r); return NULL; }

    if (row_count > 0) {
        r->rows = (corm_value_t **)calloc((size_t)row_count, sizeof(corm_value_t *));
        if (!r->rows) { free(r->column_types); free(r->column_names); free(r); return NULL; }
        for (int i = 0; i < row_count; i++) {
            r->rows[i] = (corm_value_t *)calloc((size_t)column_count, sizeof(corm_value_t));
            if (!r->rows[i]) {
                for (int j = 0; j < i; j++) free(r->rows[j]);
                free(r->rows); free(r->column_types); free(r->column_names); free(r);
                return NULL;
            }
        }
    }

    return r;
}

void corm_result_retain(corm_result_t *r) {
    if (r) r->refcount++;
}

void corm_result_release(corm_result_t *r) {
    if (!r) return;
    if (--r->refcount > 0) return;

    for (int i = 0; i < r->row_count; i++) {
        if (r->rows && r->rows[i]) {
            for (int j = 0; j < r->column_count; j++) {
                corm_value_t *v = &r->rows[i][j];
                if ((v->type == CORM_TEXT || v->type == CORM_STRING) && v->v.s)
                    free(v->v.s);
                if (v->type == CORM_BLOB && v->v.blob.data)
                    free(v->v.blob.data);
            }
            free(r->rows[i]);
        }
    }
    free(r->rows);
    for (int j = 0; j < r->column_count; j++) {
        free(r->column_names[j]);
    }
    free(r->column_names);
    free(r->column_types);
    free(r);
}

int corm_result_row_count(corm_result_t *r) { return r->row_count; }
int corm_result_col_count(corm_result_t *r) { return r->column_count; }
const char *corm_result_col_name(corm_result_t *r, int col) {
    if (col < 0 || col >= r->column_count) return NULL;
    return r->column_names[col];
}
corm_field_type_t corm_result_col_type(corm_result_t *r, int col) {
    if (col < 0 || col >= r->column_count) return CORM_TEXT;
    return r->column_types[col];
}

corm_value_t *corm_result_value(corm_result_t *r, int row, int col) {
    if (row < 0 || row >= r->row_count) return NULL;
    if (col < 0 || col >= r->column_count) return NULL;
    return &r->rows[row][col];
}

bool corm_result_next(corm_result_t *r) {
    r->current_row++;
    return r->current_row < r->row_count;
}

void corm_result_reset(corm_result_t *r) {
    r->current_row = -1;
}

/* ── Scalar helpers ── */

int64_t corm_result_int(corm_result_t *r, int row, int col) {
    corm_value_t *v = corm_result_value(r, row, col);
    if (!v || v->is_null) return 0;
    return v->v.i;
}

double corm_result_double(corm_result_t *r, int row, int col) {
    corm_value_t *v = corm_result_value(r, row, col);
    if (!v || v->is_null) return 0.0;
    return v->v.f;
}

const char *corm_result_string(corm_result_t *r, int row, int col) {
    corm_value_t *v = corm_result_value(r, row, col);
    if (!v || v->is_null) return NULL;
    return v->v.s;
}

bool corm_result_bool(corm_result_t *r, int row, int col) {
    corm_value_t *v = corm_result_value(r, row, col);
    if (!v || v->is_null) return false;
    return v->v.b;
}

bool corm_result_is_null(corm_result_t *r, int row, int col) {
    corm_value_t *v = corm_result_value(r, row, col);
    return !v || v->is_null;
}
