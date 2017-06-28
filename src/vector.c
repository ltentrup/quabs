
#include <assert.h>
#include <unistd.h>

#include "vector.h"
#include "logging.h"
#include "stdbool.h"

#define INITIAL_SIZE 3
#define INCREASE_FACTOR 2

_Static_assert(INITIAL_SIZE > 0, "Initial size must be greater than 0");
_Static_assert(INCREASE_FACTOR > 1, "Increase factor must be greater than 1");


/* Helper */

static void vector_increase(vector* v) {
    v->size *= INCREASE_FACTOR;
    void** newdata = malloc(sizeof(void*) * v->size);
    for (size_t i = 0; i < v->count; i++) {
        newdata[i] = v->data[i];
    }
    free(v->data);
    v->data = newdata;
}

static void int_vector_set_size(int_vector* v, size_t size) {
    v->size = size;
    int* newdata = malloc(sizeof(int) * v->size);
    for (size_t i = 0; i < v->count; i++) {
        newdata[i] = v->data[i];
    }
    free(v->data);
    v->data = newdata;
}

static void int_vector_increase(int_vector* v) {
    int_vector_set_size(v, v->size * INCREASE_FACTOR);
}

/* Interface */

vector* vector_init() {
    vector* v = malloc(sizeof(vector));
    v->data = malloc(sizeof(void*) * INITIAL_SIZE);
    v->count = 0;
    v->size = INITIAL_SIZE;
    return v;
}

void vector_reset(vector* v) {
    v->count = 0;
}

void vector_free(vector* v) {
    free(v->data);
    free(v);
}

size_t vector_count(const vector* v) {
    return v->count;
}

void* vector_get(const vector* v, size_t i) {
    assert (v->count > i);
    return v->data[i];
}

void vector_set(vector* v, size_t i, void* value) {
    assert (v->count > i);
    v->data[i] = value;
}

// returns absolute position, and returns -1 in case the element is not contained
size_t vector_find(vector* v, void* value) {
    for (size_t i = 0; i < v->count; i++) {
        if (v->data[i] == value) {
            return i;
        }
    }
    return VECTOR_NOT_FOUND;
}

// returns absolute position, and returns -1 in case the element is not contained
size_t vector_find_sorted(vector* v, void* value) {
    ssize_t imin = 0, imax = v->count - 1;
    while (imax >= imin) {
        ssize_t imid = imin + ((imax - imin) / 2); // prevent overflow
        if (v->data[imid] == value) {
            return imid;
        } else if (v->data[imid] < value) {
            imin = imid + 1;
        } else {
            imax = imid - 1;
        }
    }
    return VECTOR_NOT_FOUND;
}

bool vector_contains(vector* v, void* value) {
    return vector_find(v, value) != VECTOR_NOT_FOUND;
}

bool vector_contains_sorted(vector* v, void* value) {
    return vector_find_sorted(v, value) != VECTOR_NOT_FOUND;
}

void vector_add(vector* v, void* value) {
    if (v->size == v->count) {
        vector_increase(v);
    }
    v->data[v->count] = value;
    v->count += 1;
}

void vector_add_sorted(vector* v, void* value) {
    if (v->size == v->count) {
        vector_increase(v);
    }
    ssize_t imin = 0, imax = v->count - 1;
    while (imax >= imin) {
        ssize_t imid = imin + ((imax - imin) / 2); // prevent overflow
        if (v->data[imid] == value) {
            return;
        } else if (v->data[imid] < value) {
            imin = imid + 1;
        } else {
            imax = imid - 1;
        }
    }
    
    for (size_t i = imin; i < v->count; i++) {
        void* swap = v->data[i];
        v->data[i] = value;
        value = swap;
    }
    v->data[v->count] = value;
    v->count += 1;
}

void vector_insert_at(vector* v, size_t i, void* value) {
    if (v->size == v->count) {
        vector_increase(v);
    }
    for (size_t j = i; j < v->count; j++) {
        void* swap = v->data[j];
        v->data[j] = value;
        value = swap;
    }
    v->data[v->count] = value;
    v->count += 1;
}

void vector_print(vector* v) {
    logging_print("Vector (%ld,%ld) %s",v->count,v->size,"");
    for (size_t j = 0; j < v->count; j++) {
        logging_print(" %p", v->data[j]);
    }
    logging_print("\n%s","");
}

void vector_resize(vector* v, size_t value) {
    v->count = value;
}

bool vector_is_sorted(vector* v) {
    for (size_t i = 1; i < vector_count(v); i++) {
        if (vector_get(v, i-1) > vector_get(v, i)) {
            return false;
        }
    }
    return true;
}

void vector_compress(vector* v) {
    size_t j = 0;
    for (size_t i = 0; i < vector_count(v); i++) {
        assert(i >= j);
        vector_set(v, j, vector_get(v, i));
        if (vector_get(v, j) != NULL) {
            j++;
        }
    }
    vector_resize(v, j);
}

void vector_remove_index(vector* v, size_t i) {
    assert(i < v->count);
    v->count = v->count - 1; // yes, before the loop
    for (; i < v->count; i++) {
        v->data[i] = v->data[i+1];
    }
}

bool vector_remove(vector* v, void* value) {
    size_t i;
    for (i = 0; i < v->count; i++) {
        if (v->data[i] == value) {
            break;
        }
    }
    if (i == v->count) {
        return false;
    }
    vector_remove_index(v, i);
    return true;
}

void vector_copy(const vector* src, vector* dst) {
    vector_reset(dst);
    for (size_t i = 0; i < vector_count(src); i++) {
        vector_add(dst, vector_get(src, i));
    }
}


int_vector* int_vector_init() {
    int_vector* v = malloc(sizeof(int_vector));
    v->data = malloc(sizeof(int) * INITIAL_SIZE);
    v->count = 0;
    v->size = INITIAL_SIZE;
    return v;
}

void int_vector_reset(int_vector* v) {
    v->count = 0;
}

void int_vector_free(int_vector* v) {
    free(v->data);
    free(v);
}

size_t int_vector_count(const int_vector* v) {
    return v->count;
}

int int_vector_get(const int_vector* v, size_t i) {
    assert (v->count > i);
    return v->data[i];
}

void int_vector_set(int_vector* v, size_t i, int value) {
    assert (v->count > i);
    v->data[i] = value;
}

// returns absolute position, and returns -1 in case the element is not contained
size_t int_vector_find(int_vector* v, int value) {
    for (size_t i = 0; i < v->count; i++) {
        if (v->data[i]==value) {
            return i;
        }
    }
    return VECTOR_NOT_FOUND;
}

// returns absolute position, and returns -1 in case the element is not contained
size_t int_vector_find_sorted(int_vector* v, int value) {
    ssize_t imin = 0, imax = v->count - 1;
    while (imax >= imin) {
        ssize_t imid = imin + ((imax - imin) / 2); // prevent overflow
        if (v->data[imid] == value) {
            return imid;
        } else if (v->data[imid] < value) {
            imin = imid + 1;
        } else {
            imax = imid - 1;
        }
    }
    return VECTOR_NOT_FOUND;
}

bool int_vector_contains_sorted(int_vector* v, int value) {
    return int_vector_find_sorted(v, value) != VECTOR_NOT_FOUND;
}

void int_vector_add(int_vector* v, int value) {
    if (v->size == v->count) {
        int_vector_increase(v);
    }
    v->data[v->count] = value;
    v->count += 1;
}

void int_vector_add_sorted(int_vector* v, int value) {
    if (v->size == v->count) {
        int_vector_increase(v);
    }
    ssize_t imin = 0, imax = v->count - 1;
    while (imax >= imin) {
        ssize_t imid = imin + ((imax - imin) / 2); // prevent overflow
        if (v->data[imid] == value) {
            return;
        } else if (v->data[imid] < value) {
            imin = imid + 1;
        } else {
            imax = imid - 1;
        }
    }
    
    for (size_t i = imin; i < v->count; i++) {
        int swap = v->data[i];
        v->data[i] = value;
        value = swap;
    }
    v->data[v->count] = value;
    v->count += 1;
}

int* int_vector_get_data(int_vector* v) {
    return v->data;
}

void int_vector_print(int_vector* v) {
    printf("int_vector (%zu,%zu) ", v->count, v->size);
    for (size_t j = 0; j < v->count; j++) {
        printf(" %d", v->data[j]);
    }
    printf("\n%s","");
}

bool int_vector_remove(int_vector* v, int value) {
    size_t i;
    for (i = 0; i < v->count; i++) {
        if (v->data[i] == value) {
            break;
        }
    }
    if (i == v->count) {
        return false;
    }
    int_vector_remove_index(v, i);
    return true;
}

void int_vector_remove_index(int_vector* v, size_t i) {
    v->count = v->count - 1; // yes, before the loop
    for (; i < v->count; i++) {
        v->data[i] = v->data[i+1];
    }
}

bool int_vector_is_sorted(int_vector* v) {
    for (size_t i = 1; i < int_vector_count(v); i++) {
        if (int_vector_get(v, i-1) >= int_vector_get(v, i)) {
            return false;
        }
    }
    return true;
}

bool int_vector_equal(const int_vector* lhs, const int_vector* rhs) {
    if (int_vector_count(lhs) != int_vector_count(rhs)) {
        return false;
    }
    
    for (size_t i = 0; i < int_vector_count(lhs); i++) {
        int a = int_vector_get(lhs, i);
        int b = int_vector_get(rhs, i);
        if (a != b) {
            return false;
        }
    }
    return true;
}

void int_vector_copy(const int_vector* src, int_vector* dst) {
    int_vector_reset(dst);
    for (size_t i = 0; i < int_vector_count(src); i++) {
        int_vector_add(dst, int_vector_get(src, i));
    }
}
