
#ifndef VECTOR_H
#define VECTOR_H

#include <stdlib.h>
#include <stdbool.h>
//#include "map.h"

#define VECTOR_NOT_FOUND (size_t)-1

typedef struct {
    void** data;
    size_t size;
    size_t count;
} vector;


vector* vector_init(void);
size_t vector_count(const vector* v);
void vector_add(vector* v, void* value);
void vector_add_sorted(vector* v, void* value);
void vector_set(vector* v, size_t i, void* value);
void vector_insert_at(vector* v, size_t i, void* value);
void* vector_get(const vector* v, size_t i);
size_t vector_find(vector* v, void* value);
size_t vector_find_sorted(vector* v, void* value);
bool vector_contains(vector* v, void* value);
bool vector_contains_sorted(vector* v, void* value);
void vector_free(vector* v);
void vector_print(vector* v);
void vector_reset(vector* v);
bool vector_remove(vector* v, void* value);
void vector_remove_index(vector* v, size_t i);
void vector_resize(vector* v, size_t value);
bool vector_is_sorted(vector* v);
void vector_copy(const vector* src, vector* dst);

/**
 * Removes all occurrences of NULL from vector.
 */
void vector_compress(vector* v);

typedef struct {
    int*   data;
    size_t size;
    size_t count;
} int_vector;

int_vector* int_vector_init(void);
size_t int_vector_count(const int_vector* v);
void int_vector_add(int_vector* v, int value);
void int_vector_add_sorted(int_vector* v, int value);
void int_vector_set(int_vector* v, size_t i, int value);
int int_vector_get(const int_vector* v, size_t i);
size_t int_vector_find(int_vector* v, int value);
size_t int_vector_find_sorted(int_vector* v, int value);
bool int_vector_contains_sorted(int_vector* v, int value);
int* int_vector_get_data(int_vector* v);
void int_vector_free(int_vector* v);
void int_vector_print(int_vector* v);
void int_vector_reset(int_vector* v);
bool int_vector_remove(int_vector* v, int value);
void int_vector_remove_index(int_vector* v, size_t index);
bool int_vector_is_sorted(int_vector* v);
bool int_vector_equal(const int_vector*, const int_vector*);
void int_vector_copy(const int_vector* src, int_vector* dst);

#endif
