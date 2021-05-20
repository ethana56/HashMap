#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdlib.h>
#include <stdbool.h>

typedef struct hashmap HashMap;

struct hashmap_allocator {
    void *(*alloc)(size_t);
    void (*free)(void *);
};

struct hashmap_config {
    struct hashmap_allocator allocator;
    unsigned long long (*get_hash)(void *);
    int (*compare)(void *, void *);
    void (*free_key)(void *);
    size_t *sizes;
    size_t num_sizes;
    size_t element_size;
    double load_factor;
    bool copy_elements;
};

HashMap *hashmap_new(struct hashmap_config *);
void *hashmap_get(HashMap *, void *key);
int hashmap_set(HashMap *, void *key);

#endif