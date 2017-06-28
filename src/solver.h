//
//  solver.h
//  caqe-qcir
//
//  Created by Leander Tentrup on 01.07.15.
//  Copyright (c) 2015 Saarland University. All rights reserved.
//

#ifndef __caqe_qcir__caqe__
#define __caqe_qcir__caqe__

#include <stdio.h>
#include <stdbool.h>

#include "certification.h"
#include "circuit.h"
#include "config.h"


typedef struct {
    // high level features
    bool preprocess;
    bool miniscoping;
    bool certify;
    bool statistics;
    bool partial_assignment;
    
    // low level solver features
    bool assignment_b_lit_minimization;  // minimize b_lit entry according to assignments of circuit
    bool use_partial_deref;
    bool use_combined_abstraction;
    
#ifdef PARALLEL_SOLVING
    size_t num_threads;
#endif
} SolverOptions;

typedef struct {
    SolverOptions* options;
    Circuit* circuit;
    
#ifdef CERTIFICATION
    certification cert;
#endif
} Solver;


Solver*      solver_init(SolverOptions*, Circuit*);
void         solver_free(Solver*);
SolverOptions* solver_get_default_options(void);
qbf_res     solver_sat(Solver*);
void         solver_print_statistics(Solver*);

#endif /* defined(__caqe_qcir__caqe__) */
