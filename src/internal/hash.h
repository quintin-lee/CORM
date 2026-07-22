#ifndef CROM_HASH_H
#define CROM_HASH_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "corm_pub.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct corm_hash_entry {
    char *key;
    void *value;
    struct corm_hash_entry *next;
} corm_hash_entry_t;

typedef struct {
    corm_hash_entry_t **buckets;
    size_t size;
    size_t count;
} corm_hash_t;

#define CORM_HASH_INIT { .buckets = NULL, .size = 0, .count = 0 }

static inline corm_err_t corm_hash_init(corm_hash_t *ht, size_t initial_size) {
    if (initial_size == 0) initial_size = 32;
    ht->buckets = (corm_hash_entry_t **)calloc(initial_size, sizeof(corm_hash_entry_t *));
    if (!ht->buckets) return CORM_ERR_NOMEM;
    ht->size = initial_size;
    ht->count = 0;
    return CORM_OK;
}

static inline size_t corm_hash_str(const char *key, size_t bucket_count) {
    size_t h = 5381;
    while (*key) h = ((h << 5) + h) + (unsigned char)*key++;
    return h % bucket_count;
}

static inline corm_err_t corm_hash_insert(corm_hash_t *ht, const char *key, void *value) {
    size_t idx = corm_hash_str(key, ht->size);
    corm_hash_entry_t *entry = (corm_hash_entry_t *)malloc(sizeof(corm_hash_entry_t));
    if (!entry) return CORM_ERR_NOMEM;
    entry->key = strdup(key);
    if (!entry->key) { free(entry); return CORM_ERR_NOMEM; }
    entry->value = value;
    entry->next = ht->buckets[idx];
    ht->buckets[idx] = entry;
    ht->count++;
    return CORM_OK;
}

static inline void *corm_hash_find(corm_hash_t *ht, const char *key) {
    size_t idx = corm_hash_str(key, ht->size);
    corm_hash_entry_t *entry = ht->buckets[idx];
    while (entry) {
        if (strcmp(entry->key, key) == 0) return entry->value;
        entry = entry->next;
    }
    return NULL;
}

static inline void corm_hash_remove(corm_hash_t *ht, const char *key) {
    size_t idx = corm_hash_str(key, ht->size);
    corm_hash_entry_t **pp = &ht->buckets[idx];
    while (*pp) {
        corm_hash_entry_t *entry = *pp;
        if (strcmp(entry->key, key) == 0) {
            *pp = entry->next;
            free(entry->key);
            free(entry);
            ht->count--;
            return;
        }
        pp = &entry->next;
    }
}

static inline void corm_hash_free(corm_hash_t *ht) {
    for (size_t i = 0; i < ht->size; i++) {
        corm_hash_entry_t *entry = ht->buckets[i];
        while (entry) {
            corm_hash_entry_t *next = entry->next;
            free(entry->key);
            free(entry);
            entry = next;
        }
    }
    free(ht->buckets);
    ht->buckets = NULL;
    ht->size = 0;
    ht->count = 0;
}

#ifdef __cplusplus
}
#endif

#endif /* CROM_HASH_H */
