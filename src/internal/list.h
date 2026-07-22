#ifndef CROM_LIST_H
#define CROM_LIST_H

#include <stddef.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Doubly-linked list node */
typedef struct corm_list_node {
    struct corm_list_node *prev;
    struct corm_list_node *next;
    void *data;
} corm_list_node_t;

typedef struct {
    corm_list_node_t *head;
    corm_list_node_t *tail;
    size_t count;
} corm_list_t;

#define CORM_LIST_INIT { .head = NULL, .tail = NULL, .count = 0 }

static inline void corm_list_init(corm_list_t *list) {
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

static inline corm_err_t corm_list_push_back(corm_list_t *list, void *data) {
    corm_list_node_t *node = (corm_list_node_t *)malloc(sizeof(corm_list_node_t));
    if (!node) return CORM_ERR_NOMEM;
    node->data = data;
    node->next = NULL;
    node->prev = list->tail;
    if (list->tail) list->tail->next = node;
    else list->head = node;
    list->tail = node;
    list->count++;
    return CORM_OK;
}

static inline corm_err_t corm_list_push_front(corm_list_t *list, void *data) {
    corm_list_node_t *node = (corm_list_node_t *)malloc(sizeof(corm_list_node_t));
    if (!node) return CORM_ERR_NOMEM;
    node->data = data;
    node->prev = NULL;
    node->next = list->head;
    if (list->head) list->head->prev = node;
    else list->tail = node;
    list->head = node;
    list->count++;
    return CORM_OK;
}

static inline void *corm_list_pop_back(corm_list_t *list) {
    if (!list->tail) return NULL;
    corm_list_node_t *node = list->tail;
    void *data = node->data;
    list->tail = node->prev;
    if (list->tail) list->tail->next = NULL;
    else list->head = NULL;
    free(node);
    list->count--;
    return data;
}

static inline void *corm_list_pop_front(corm_list_t *list) {
    if (!list->head) return NULL;
    corm_list_node_t *node = list->head;
    void *data = node->data;
    list->head = node->next;
    if (list->head) list->head->prev = NULL;
    else list->tail = NULL;
    free(node);
    list->count--;
    return data;
}

static inline void corm_list_remove(corm_list_t *list, corm_list_node_t *node) {
    if (node->prev) node->prev->next = node->next;
    else list->head = node->next;
    if (node->next) node->next->prev = node->prev;
    else list->tail = node->prev;
    free(node);
    list->count--;
}

static inline void corm_list_clear(corm_list_t *list) {
    corm_list_node_t *cur = list->head;
    while (cur) {
        corm_list_node_t *next = cur->next;
        free(cur);
        cur = next;
    }
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

#define corm_list_foreach(list, node) \
    for (corm_list_node_t *node = (list)->head; node; node = node->next)

#ifdef __cplusplus
}
#endif

#endif /* CROM_LIST_H */
