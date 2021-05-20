#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <float.h>

#include "hashmap.h"

struct bucket {
    void *key;
    struct bucket *next;
    unsigned long long hash;
};

struct hashmap {
    struct hashmap_allocator allocator;
    struct bucket **bucket_array;
    unsigned long long (*get_hash)(void *);
    int (*compare)(void *, void *);
    void (*free_key)(void *);
    size_t *sizes;
    size_t num_sizes;
    size_t cur_size;
    size_t num_elements;
    size_t element_size;
    double load_factor;
    bool copy_elements;
};

static struct bucket **hashmap_allocate_bucket_array(struct hashmap_allocator *allocator, size_t num_buckets) {
    struct bucket **bucket_array;
    size_t i;
    bucket_array = allocator->alloc(sizeof(struct bucket *) * num_buckets);
    if (bucket_array == NULL) {
        return NULL;
    }
    for (i = 0; i < num_buckets; ++i) {
        bucket_array[i] = NULL;
    }
    return bucket_array;
}

HashMap *hashmap_new(struct hashmap_config *config) {
    HashMap *hashmap;
    hashmap = config->allocator.alloc(sizeof(HashMap));
    if (hashmap == NULL) {
        return NULL;
    }
    hashmap->allocator = config->allocator;
    hashmap->get_hash = config->get_hash;
    hashmap->compare = config->compare;
    hashmap->sizes = hashmap->allocator.alloc(sizeof(size_t) * config->num_sizes);
    if (hashmap->sizes == NULL) {
        config->allocator.free(hashmap);
        return NULL;
    }
    memcpy(hashmap->sizes, config->sizes, sizeof(size_t) * config->num_sizes);
    hashmap->num_sizes = config->num_sizes;
    hashmap->load_factor = config->load_factor;
    hashmap->cur_size = 0;
    hashmap->bucket_array = hashmap_allocate_bucket_array(&hashmap->allocator, hashmap->sizes[hashmap->cur_size]);
    if (hashmap->bucket_array == NULL) {
        config->allocator.free(hashmap->sizes);
        config->allocator.free(hashmap);
        return NULL;
    }
    hashmap->num_elements = 0;
    hashmap->copy_elements = config->copy_elements;
    if (hashmap->copy_elements) {
        hashmap->element_size = config->element_size;
    }
    return hashmap;
}

static struct bucket *hashmap_bucket_create(struct hashmap_allocator *allocator, void *key, unsigned long long hash) {
    struct bucket *bucket;
    bucket = allocator->alloc(sizeof(struct bucket));
    if (bucket == NULL) {
        return NULL;
    }
    bucket->key = key;
    bucket->hash = hash;
    bucket->next = NULL;
    return bucket;
}

static int hashmap_add_key_to_chain(HashMap *hashmap, void *key, struct bucket **chain, unsigned long long hash) {
    struct bucket *cur_node, *new_node;
    cur_node = *chain;
    while (cur_node != NULL && cur_node->next != NULL) {
        if (hashmap->compare(cur_node->key, key) == 0) {
            hashmap->free_key(cur_node->key);
            if (hashmap->copy_elements) {
                hashmap->allocator.free(cur_node->key);
            }
            cur_node->key = key;
            return 0;
        }
        cur_node = cur_node->next;
    }
    new_node = hashmap_bucket_create(&hashmap->allocator, key, hash);
    if (new_node == NULL) {
        return -1;
    }
    if (cur_node == NULL) {
        *chain = new_node;
    } else {
        cur_node->next = new_node;
    }
    ++hashmap->num_elements;
    return 0;
}

static void *hashmap_get_key_to_use(HashMap *hashmap, void *key) {
    void *key_to_use;
    if (hashmap->copy_elements) {
        key_to_use = hashmap->allocator.alloc(hashmap->element_size);
        if (key_to_use == NULL) {
            return NULL;
        }
        memcpy(key_to_use, key, hashmap->element_size);
    } else {
        key_to_use = key;
    }
    return key_to_use;
}

static int hashmap_add_key(HashMap *hashmap, void *key) {
    struct bucket **chain;
    void *key_to_use;
    size_t index;
    unsigned long long hash;
    key_to_use = hashmap_get_key_to_use(hashmap, key);
    if (key_to_use == NULL) {
        return -1;
    }
    hash = hashmap->get_hash(key_to_use);
    index = hash % hashmap->sizes[hashmap->cur_size];
    chain = hashmap->bucket_array + index;
    if (hashmap_add_key_to_chain(hashmap, key_to_use, chain, hash) < 0) {
        if (key != key_to_use) {
            hashmap->allocator.free(key_to_use);
        }
        return -1;
    }
    return 0;
}

static void hashmap_add_bucket_to_chain(HashMap *hashmap, struct bucket **chain, struct bucket *node) {
    struct bucket *cur_node;
    if (*chain == NULL) {
        *chain = node;
        return;
    }
    cur_node = *chain;
    while (cur_node->next != NULL) {
        cur_node = cur_node->next;
    }
    cur_node->next = node;
}

static void hashmap_rehash(HashMap *hashmap, struct bucket **bucket_array, size_t size) {
    size_t i;
    for (i = 0; i < size; ++i) {
        struct bucket *cur_node;
        cur_node = bucket_array[i];
        while (cur_node != NULL) {
            struct bucket *next_node;
            size_t index;
            next_node = cur_node->next;
            cur_node->next = NULL;
            index = cur_node->hash % hashmap->sizes[hashmap->cur_size];
            hashmap_add_bucket_to_chain(hashmap, (hashmap->bucket_array + index), cur_node);
            cur_node = next_node;
        }
    }
}

static int hashmap_resize(HashMap *hashmap) {
    struct bucket **new_bucket_array, **old_bucket_array;
    if (hashmap->cur_size == hashmap->num_sizes - 1) {
        return 0;
    }
    new_bucket_array = hashmap_allocate_bucket_array(&hashmap->allocator, 
                        hashmap->sizes[hashmap->cur_size + 1]);
    if (new_bucket_array == NULL) {
        return -1;
    }
    ++hashmap->cur_size;
    old_bucket_array = hashmap->bucket_array;
    hashmap->bucket_array = new_bucket_array;
    hashmap_rehash(hashmap, old_bucket_array, hashmap->sizes[hashmap->cur_size - 1]);
    hashmap->allocator.free(old_bucket_array);
    return 0;
}

static struct bucket *hashmap_find_in_chain(HashMap *hashmap, struct bucket **chain, void *key) {
    struct bucket *cur_bucket;
    cur_bucket = *chain;
    while (cur_bucket != NULL && hashmap->compare(cur_bucket->key, key) != 0) {
        cur_bucket = cur_bucket->next;
    }
    return cur_bucket;
}

void *hashmap_get(HashMap *hashmap, void *key) {
    struct bucket **chain, *found;
    size_t hash, index;
    hash = hashmap->get_hash(key);
    index = hash % hashmap->sizes[hashmap->cur_size];
    chain = hashmap->bucket_array + index;
    found = hashmap_find_in_chain(hashmap, chain, key);
    return found == NULL ? NULL : found->key;
}

int hashmap_set(HashMap *hashmap, void *key) {
    if (((double)hashmap->num_elements / (double)hashmap->sizes[hashmap->cur_size]) > hashmap->load_factor) {
        if (hashmap_resize(hashmap) < 0) {
            return -1;
        }
    }
    return hashmap_add_key(hashmap, key);
}

