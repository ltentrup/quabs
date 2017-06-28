//
//  certification.h
//  caqe-qcir
//
//  Created by Leander Tentrup on 06.01.16.
//  Copyright © 2016 Saarland University. All rights reserved.
//

#ifndef certification_h
#define certification_h

#include "aiger.h"
#include "circuit.h"
//#include "circuit_abstraction.h"
#include "queue.h"
#include "map.h"

typedef enum {
    SKOLEM,
    HERBRAND
} certificate_t;

typedef struct {
    aiger* skolem;
    aiger* herbrand;
    
    unsigned next_lit_skolem;
    unsigned next_lit_herbrand;
    
    int_queue queues[2];
    unsigned current_queue;
    
    // incremental function building
    map* function_lit;      // maps var_id to current function literal
    map* precondition_lit;  // maps var_id to last precondition
} certification;

typedef struct circuit_abstraction CircuitAbstraction;


void certification_init(certification*, Circuit*);

void certification_import_variables(certification*, Circuit*);

void certification_add_literal(certification*, lit_t);
void certification_add_b_literal(certification*, CircuitAbstraction*, quantifier_type, var_t);
void certification_add_t_literal(certification*, CircuitAbstraction*, quantifier_type, var_t, lit_t b_lit);
void certification_define_b_literal(certification*, CircuitAbstraction*, quantifier_type, var_t);
unsigned certification_define_or(certification*, quantifier_type);
unsigned certification_define_and(certification*, quantifier_type);
void certification_push(certification*);
void certification_pop(certification*);
void certification_clear(certification*);
void certification_add_aiger_literal(certification*, lit_t);
void certification_append_function_case(certification*, CircuitAbstraction*);
bool certification_queue_is_empty(certification*);

void certification_define_outputs(certification*, CircuitAbstraction*, qbf_res);
void certification_print(certification*, qbf_res);

#endif /* certification_h */
