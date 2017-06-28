//
//  bit_vector.h
//  caqe
//
//  Copyright (c) 2015, Leander Tentrup, Saarland University
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  "Software"), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be
//  included in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

#ifndef __caqe__bit_vector__
#define __caqe__bit_vector__

#include <stdbool.h>
#include <stddef.h>

#define BIT_VECTOR_NO_ENTRY UINT64_MAX

typedef struct bit_vector bit_vector;

bit_vector* bit_vector_init(size_t offset, size_t max_num);
void bit_vector_free(bit_vector*);
void bit_vector_reset(bit_vector*);
void bit_vector_add(bit_vector*, size_t);
void bit_vector_remove(bit_vector*, size_t);
bool bit_vector_contains(bit_vector*, size_t);
void bit_vector_update_or(bit_vector* target, const bit_vector* source);
void bit_vector_update_and(bit_vector* target, const bit_vector* source);
size_t bit_vector_min(bit_vector*);
size_t bit_vector_max(bit_vector*);
bool bit_vector_equal(bit_vector*, bit_vector*);

// Iteration
size_t bit_vector_init_iteration(bit_vector*);
bool bit_vector_iterate(bit_vector*);
size_t bit_vector_next(bit_vector*);


#endif /* defined(__caqe__bit_vector__) */
