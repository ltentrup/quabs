//
//  circuit_abstraction.h
//  caqe-qcir
//
//  Created by Leander Tentrup on 03.07.15.
//  Copyright (c) 2015 Saarland University. All rights reserved.
//

#ifndef __caqe_qcir__circuit_abstraction__
#define __caqe_qcir__circuit_abstraction__

#include <stdio.h>

#include "solver.h"
#include "circuit.h"
#include "vector.h"
#include "satsolver.h"
#include "statistics.h"

#ifdef PARALLEL_SOLVING
#include <pthread.h>
#include "semaphore.h"
#endif


typedef struct circuit_abstraction CircuitAbstraction;

struct circuit_abstraction {
    SolverOptions* options;
    
    // certification
    certification* cert;
    
    qbf_res result;
    
    Scope* scope;
    CircuitAbstraction* prev;
    CircuitAbstraction** next;
    
    SATSolver* sat;       // abstraction
    SATSolver* negation;  // dual abstraction

    
    int_vector* b_lits;
    int_vector* t_lits;
    int_vector* assumptions;
    bit_vector* entry;
    int_vector* unsat_core;
    int_vector* local_unsat_core;
    int_vector* sat_solver_assumptions;
    
    Stats* statistics;
#ifdef PARALLEL_SOLVING
    pthread_mutex_t mutex;
    pthread_t thread;
    semaphore has_entry;
    semaphore sub_finished;
    size_t num_started;
#endif
};

CircuitAbstraction* circuit_abstraction_init(SolverOptions*, certification*, Scope*, CircuitAbstraction* prev);
void circuit_abstraction_free(CircuitAbstraction*);
void circuit_abstraction_free_recursive(CircuitAbstraction*);

void circuit_abstraction_get_assumptions(CircuitAbstraction*);
void circuit_abstraction_assume_t_literals(CircuitAbstraction*, bool);
void circuit_abstraction_dual_propagation(CircuitAbstraction*);
void circuit_abstraction_get_unsat_core(CircuitAbstraction*);
void circuit_abstraction_adjust_local_unsat_core(CircuitAbstraction*, CircuitAbstraction*);

var_t node_to_t_lit(const Circuit*, const node_shared*);
var_t node_to_b_lit(const Circuit*, const node_shared*);
var_t var_id_to_t_lit(const Circuit*, var_t);
var_t var_id_to_b_lit(const Circuit*, var_t);
var_t t_lit_to_b_lit(const Circuit*, var_t);
var_t t_lit_to_var_id(const Circuit*, var_t);
var_t b_lit_to_var_id(const Circuit*, var_t);
var_t b_lit_to_t_lit(const Circuit*, var_t);


#endif /* defined(__caqe_qcir__circuit_abstraction__) */
