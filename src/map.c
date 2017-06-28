#include <assert.h>

#include "map.h"
#include "logging.h"


#define INITIAL_MAP_SIZE 10

// From https://gist.github.com/badboy/6267743
static int hash32shiftmult(int key) {
    int c2 = 0x27d4eb2d; // a prime or an odd constant
    key = (key ^ 61) ^ (key >> 16);
    key = key + (key << 3);
    key = key ^ (key >> 4);
    key = key * c2;
    key = key ^ (key >> 15);
    return key;
}

static size_t hash_function(int key, size_t size) {
    return (size_t)hash32shiftmult(key) % size;
}

map* map_init() {
    return map_init_size(INITIAL_MAP_SIZE);
}

map* map_init_size(size_t size) {
    map* container = malloc(sizeof(map));
    container->data = calloc(sizeof(map_entry*), size);
    container->count = 0;
    container->size = size;
    return container;
}

static map_entry* map_get_entry(const map* container, int key) {
    size_t hash = hash_function(key, container->size);
    assert(hash < container->size);
    
    map_entry* entry = container->data[hash];
    while (entry != NULL && entry->key != key) {
        entry = entry->next;
    }
    return entry;
}

bool map_contains(const map* container, int key) {
    return map_get_entry(container, key) != NULL;
}

void* map_get(const map* container, int key) {
    map_entry* entry = map_get_entry(container, key);
    if (entry != NULL) {
        assert(entry->key == key);
        return entry->data;
    }
    return NULL;
}

// Same as map_add, but works only if the element is already in the map
void map_update(map* container, int key, void* data) {
    map_entry* entry = map_get_entry(container, key);
    assert(entry != NULL);
    entry->data = data;
}

void map_add(map* container, int key, void* data) {
    assert(!map_get(container, key));
    size_t hash = hash_function(key, container->size);
    assert(hash < container->size);
    
    map_entry* new_entry = malloc(sizeof(map_entry));
    new_entry->key = key;
    new_entry->data = data;
    new_entry->next = container->data[hash];
    container->data[hash] = new_entry;
    
    container->count++;
    
    if (container->count > container->size + container->size / 2) {
        map_resize(container, 2 * container->size);
    }
}

void map_resize(map* container, size_t new_size) {
    logging_debug("Resizing container to size %zu\n", new_size);
    size_t old_size = container->size;
    map_entry** old_data = container->data;
    
    container->size = new_size;
    container->data = calloc(sizeof(map_entry*), new_size);
    
    for (size_t i = 0; i < old_size; i++) {
        map_entry* entry = old_data[i];
        while (entry != NULL) {
            map_entry* next = entry->next;
            size_t new_hash = hash_function(entry->key, container->size);
            assert(new_hash < container->size);
            
            entry->next = container->data[new_hash];
            container->data[new_hash] = entry;
            
            entry = next;
        }
    }
    
    free(old_data);
}

static void map_free_entry(map_entry* entry) {
    free(entry);
}

void map_remove(map* container, int key) {
    size_t hash = hash_function(key, container->size);
    assert(hash < container->size);
    
    map_entry* entry = container->data[hash];
    map_entry* prev = NULL;
    while (entry != NULL && entry->key != key) {
        prev = entry;
        entry = entry->next;
    }
    if (entry == NULL) {
        return;
    }
    
    if (prev == NULL) {
        container->data[hash] = entry->next;
    } else {
        prev->next = entry->next;
    }
    
    map_free_entry(entry);
}

void map_free(map* container) {
    for (size_t i = 0; i < container->size; i++) {
        map_entry* next_e;
        for (map_entry* e = container->data[i]; e != NULL; e = next_e) {
            next_e = e->next;
            map_free_entry(e);
        }
    }
    free(container);
}
