//
//  queue.h
//  caqe
//
//  Created by Leander Tentrup on 29.02.16.
//  Copyright © 2016 Saarland University. All rights reserved.
//

#ifndef queue_h
#define queue_h

#include <stdbool.h>

typedef struct int_queue_element int_queue_element;

struct int_queue_element {
    int value;
    int_queue_element* next;
};

typedef struct {
    int_queue_element* first;
    int_queue_element* last;
} int_queue;

void int_queue_init(int_queue*);
bool int_queue_is_empty(int_queue*);
void int_queue_push(int_queue*, int);
int int_queue_pop(int_queue*);
bool int_queue_has_at_least_one_element(int_queue*);
bool int_queue_has_at_least_two_elements(int_queue*);
bool int_queue_has_more_than_two_elements(int_queue*);
void int_queue_print(int_queue*);

#endif /* queue_h */
