//
//  aiger_helper.c
//  caqe-qcir
//
//  Created by Leander Tentrup on 06.04.16.
//  Copyright © 2016 Saarland University. All rights reserved.
//

#include "aiger_helper.h"

#include <assert.h>

static unsigned new_aiger_lit(unsigned* next_lit) {
    return ++(*next_lit) * 2;
}

/**
 * Encodes the AND of aiger literals given in int_vector such that the result
 * is stored in dest_aiger_lit
 *
 * @note modifies aiger_lits
 */
void encode_and_as(aiger* aig, unsigned* next_lit, int_queue* aiger_lits, unsigned dest_aiger_lit) {
    while (int_queue_has_more_than_two_elements(aiger_lits)) {
        unsigned lhs = (unsigned)int_queue_pop(aiger_lits);
        unsigned rhs = (unsigned)int_queue_pop(aiger_lits);
        unsigned new_lit = new_aiger_lit(next_lit);
        aiger_add_and(aig, new_lit, lhs, rhs);
        int_queue_push(aiger_lits, (int)new_lit);
    }
    unsigned lhs = aiger_true, rhs = aiger_true;
    
    if (int_queue_has_at_least_one_element(aiger_lits)) {
        lhs = (unsigned)int_queue_pop(aiger_lits);
    }
    if (int_queue_has_at_least_one_element(aiger_lits)) {
        rhs = (unsigned)int_queue_pop(aiger_lits);
    }
    aiger_add_and(aig, aiger_strip(dest_aiger_lit), lhs, rhs);
}
/**
 * Encodes the OR of aiger literals given in int_vector such that the result
 * is stored in dest_aiger_lit
 *
 * @note modifies aiger_lits
 */
void encode_or_as(aiger* aig, unsigned* next_lit, int_queue* aiger_lits, unsigned dest_aiger_lit) {
    // negate literals in aiger_lits first
    for (int_queue_element* element = aiger_lits->first; element != NULL; element = element->next) {
        element->value = (int)aiger_not(element->value);
    }
    encode_and_as(aig, next_lit, aiger_lits, dest_aiger_lit);
}

unsigned encode_and(aiger* aig, unsigned* next_lit, int_queue* aiger_lits) {
    
    if (!int_queue_has_at_least_two_elements(aiger_lits)) {
        // has 0 or 1 elements
        if (int_queue_is_empty(aiger_lits)) {
            return aiger_true;
        } else {
            return (unsigned)int_queue_pop(aiger_lits);
        }
    }
    
    assert(int_queue_has_at_least_two_elements(aiger_lits));
    
    do {
        unsigned lhs = (unsigned)int_queue_pop(aiger_lits);
        unsigned rhs = (unsigned)int_queue_pop(aiger_lits);
        unsigned new_lit = new_aiger_lit(next_lit);
        aiger_add_and(aig, new_lit, lhs, rhs);
        int_queue_push(aiger_lits, (int)new_lit);
    } while (int_queue_has_more_than_two_elements(aiger_lits));
    
    assert(int_queue_has_at_least_one_element(aiger_lits));
    
    unsigned lhs = aiger_true, rhs = aiger_true;
    
    if (int_queue_has_at_least_one_element(aiger_lits)) {
        lhs = (unsigned)int_queue_pop(aiger_lits);
    }
    if (int_queue_has_at_least_one_element(aiger_lits)) {
        rhs = (unsigned)int_queue_pop(aiger_lits);
    }
    unsigned result = new_aiger_lit(next_lit);
    aiger_add_and(aig, result, lhs, rhs);
    return result;
}

unsigned encode_or(aiger* aig, unsigned* next_lit, int_queue* aiger_lits) {
    // negate literals in aiger_lits first
    for (int_queue_element* element = aiger_lits->first; element != NULL; element = element->next) {
        element->value = (int)aiger_not(element->value);
    }
    return aiger_not(encode_and(aig, next_lit, aiger_lits));
}
