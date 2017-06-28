//
//  queue.c
//  caqe
//
//  Created by Leander Tentrup on 29.02.16.
//  Copyright © 2016 Saarland University. All rights reserved.
//

#include "queue.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

void int_queue_init(int_queue* queue) {
    queue->first = queue->last = NULL;
}

bool int_queue_is_empty(int_queue* queue) {
    return queue->last == NULL;
}

void int_queue_push(int_queue* queue, int value) {
    int_queue_element* element = malloc(sizeof(int_queue_element));
    element->value = value;
    element->next = NULL;
    if (queue->first == NULL) {
        assert(queue->last == NULL);
        queue->first = queue->last = element;
    } else {
        assert(queue->last != NULL);
        int_queue_element* previous_last = queue->last;
        queue->last = element;
        previous_last->next = element;
    }
}

int int_queue_pop(int_queue* queue) {
    assert(!int_queue_is_empty(queue));
    int_queue_element* element = queue->first;
    assert(element != NULL);
    
    const int value = element->value;
    
    if (queue->first == queue->last) {
        // pop last element
        queue->first = queue->last = NULL;
    } else {
        queue->first = element->next;
    }
    
    free(element);
    
    return value;
}

bool int_queue_has_at_least_one_element(int_queue* queue) {
    return queue->first != NULL;
}

bool int_queue_has_at_least_two_elements(int_queue* queue) {
    return queue->first != NULL && queue->first != queue->last;
}

bool int_queue_has_more_than_two_elements(int_queue* queue) {
    if (queue->first == NULL) {
        return false;
    }
    if (queue->first->next == NULL) {
        return false;
    }
    if (queue->first->next == queue->last) {
        return false;
    }
    return true;
}

void int_queue_print(int_queue* queue) {
    printf("int_queue");
    for (int_queue_element* element = queue->first; element != NULL; element = element->next) {
        printf(" %d", element->value);
    }
    printf("\n");
}
