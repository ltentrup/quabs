//
//  fixpoint.h
//  caqe-qcir
//
//  Created by Leander Tentrup on 26.04.16.
//  Copyright © 2016 Saarland University. All rights reserved.
//

#ifndef fixpoint_h
#define fixpoint_h

#include "aiger.h"
#include "circuit.h"
#include "circuit_abstraction.h"
#include "map.h"

typedef enum {
    FIXPOINT_INDUCTIVE,
    FIXPOINT_NO_FIXPOINT,
    FIXPOINT_NO_RESULT
} fixpoint_res;

typedef struct {
    aiger* instance;
    Circuit* circuit;
    SolverOptions* options;
    CircuitAbstraction* universal;
    CircuitAbstraction* existential;
    map* prime_mapping;
    map* unprime_mapping;
    unsigned constant_zero;
    
    Gate* fixpoint;
    Gate* fixpoint_prime;
    
    // incremental sat queries
    int_queue universal_incremental;
    int_queue existential_incremental;
    int incremental_lit;
    int next_lit;
    vector* refinements;
    int_vector* excluded_states;
    
    Stats* num_refinements;
    Stats* exclusions;
    Stats* cube_size;
    Stats* outer_sat;
    Stats* inner_sat;
    Stats* sat_refinements;
    Stats* unsat_refinements;
} Fixpoint;


Fixpoint* fixpoint_init_from_aiger(aiger*);
fixpoint_res fixpoint_solve(Fixpoint*);
void fixpoint_aiger_preprocess(aiger*);

#endif /* fixpoint_h */
