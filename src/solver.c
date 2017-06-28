//
//  caqe.c
//  caqe-qcir
//
//  Created by Leander Tentrup on 01.07.15.
//  Copyright (c) 2015 Saarland University. All rights reserved.
//

#include <stdlib.h>
#include <assert.h>

#include "solver.h"
#include "circuit.h"
#include "circuit_print.h"
#include "vector.h"
#include "logging.h"
#include "circuit_abstraction.h"
#include "vector.h"
#include "util.h"

#ifdef PARALLEL_SOLVING
#include "semaphore.h"

struct solver_argument {
    Solver* solver;
    CircuitAbstraction* abstraction;
};
#endif

typedef struct {
    Solver public;
    CircuitAbstraction* abstraction;

#ifdef PARALLEL_SOLVING
    semaphore num_threads;
#endif
    
    Stats* encoding;
    Stats* preprocessing;
    Stats* building_abstraction;
    Stats* solving;
} solver_private;

#ifdef PARALLEL_SOLVING
static void* solve_sub_untyped(void* data);
#endif

static CircuitAbstraction* build_circuit_abstraction(Solver* solver, Scope* scope, CircuitAbstraction* prev) {
    CircuitAbstraction* abstraction = circuit_abstraction_init(solver->options, &solver->cert, scope, prev);
    for (size_t i = 0; i < scope->num_next; i++) {
        abstraction->next[i] = build_circuit_abstraction(solver, scope->next[i], abstraction);

#ifdef PARALLEL_SOLVING
        if (scope->num_next > 1) {
            semaphore_init(&abstraction->next[i]->has_entry, 0);
            struct solver_argument* arg = malloc(sizeof(struct solver_argument));
            arg->solver = solver;
            arg->abstraction = abstraction->next[i];
            pthread_create(&abstraction->next[i]->thread, NULL, solve_sub_untyped, arg);
        }
#endif
    }
    
#ifndef NDEBUG
    // check consistency of interface literals
    // for every b_lit in abstraction, there is a corresponding t_lit in one of its inner abstractions
    for (size_t i = 0; i < int_vector_count(abstraction->b_lits); i++) {
        const lit_t b_lit = int_vector_get(abstraction->b_lits, i);
        const lit_t t_lit = b_lit_to_t_lit(solver->circuit, b_lit);
        size_t num_contained = 0;
        for (size_t j = 0; j < scope->num_next; j++) {
            CircuitAbstraction* next = abstraction->next[j];
            if (int_vector_contains_sorted(next->t_lits, t_lit)) {
                num_contained++;
            }
        }
        //const var_t node_id = t_lit_to_var_id(solver->circuit, t_lit);
        assert(num_contained > 0);
    }
#endif
    return abstraction;
}

static void set_value_and_evaluate(Solver* solver, CircuitAbstraction* abstraction) {
    Circuit* circuit = solver->circuit;
    
    // Assign variables of current level according to SAT solver
    for (size_t i = 0; i < vector_count(abstraction->scope->vars); i++) {
        const Var* var = vector_get(abstraction->scope->vars, i);
        const lit_t sat_var = create_lit(var->shared.id, false);
        const int value = satsolver_value(abstraction->sat, sat_var);
        circuit_set_value(circuit, var->shared.id, value * abstraction->scope->scope_id);
        logging_debug("%d ", create_lit_from_value(var->shared.id, value));
    }
    logging_debug("\n");
    
    if (solver->options->assignment_b_lit_minimization) {
        circuit_evaluate_max(circuit, abstraction->scope->scope_id);
    }
}

static void refine(CircuitAbstraction* abstraction, CircuitAbstraction* child) {
    Circuit* circuit = abstraction->scope->circuit;
    
    logging_debug("refine: ");
    for (var_t failed_t_lit = bit_vector_init_iteration(child->entry); bit_vector_iterate(child->entry); failed_t_lit = bit_vector_next(child->entry)) {
        assert(failed_t_lit > 0);
        const lit_t failed_var = t_lit_to_var_id(circuit, failed_t_lit);
        const lit_t b_lit = t_lit_to_b_lit(circuit, failed_t_lit);
        
        if (!int_vector_contains_sorted(abstraction->b_lits, b_lit)) {
            satsolver_add(abstraction->sat, failed_t_lit);
            logging_debug("t%d ", failed_var);
            assert(!int_vector_contains_sorted(abstraction->assumptions, b_lit));
            continue;
        }
        
        if (!int_vector_contains_sorted(abstraction->assumptions, b_lit)) {
            assert(false);
            continue;
        }
        satsolver_add(abstraction->sat, b_lit);
        logging_debug("b%d ", failed_var);
    }
    if (child->scope->node != 0) {
        const lit_t failed_var = child->scope->node;
        const lit_t b_lit = var_id_to_b_lit(circuit, failed_var);
        satsolver_add(abstraction->sat, b_lit);
        logging_debug("b%d ", failed_var);
    }
    satsolver_add(abstraction->sat, 0);
    logging_debug("\n");
    
    if (abstraction->scope->scope_id > 1 && abstraction->options->assignment_b_lit_minimization) {
        circuit_evaluate_max(circuit, abstraction->scope->scope_id - 1);
    }
}

/*void reassume_failed_assumption_minimization(Solver* solver, CircuitAbstraction* abstraction) {
    solver_private* private = (solver_private*)solver;
    for (size_t i = 0; i < int_vector_count(private->unsat_core); i++) {
        int t_lit = int_vector_get(private->unsat_core, i);
        satsolver_assume(abstraction->sat, -t_lit);
    }
    sat_res result = satsolver_sat(abstraction->sat);
    assert(result == SATSOLVER_UNSATISFIABLE);
    
    get_unsat_core(solver, abstraction);
}*/

/*void print_counterexample(Solver* solver, CircuitAbstraction* abstraction) {
    Circuit* circuit = solver->circuit;
    printf("C ");
    for (size_t i = 0; i < vector_count(circuit->vars); i++) {
        Var* var = vector_get(circuit->vars, i);
        if (var->scope != abstraction->scope) {
            continue;
        }
        if (var->shared.value == 0) {
            continue;
        }
        printf("%d ", var->shared.value > 0 ? var->shared.orig_id : -var->shared.orig_id);
    }
    printf("0 ");
    assert(abstraction->next != NULL);
    for (size_t i = 0; i < vector_count(circuit->vars); i++) {
        Var* var = vector_get(circuit->vars, i);
        if (var->scope != abstraction->next->scope) {
            continue;
        }
        if (var->shared.value <= 0) {
            continue;
        }
        printf("%d ", var->shared.value > 0 ? var->shared.orig_id : -var->shared.orig_id);
    }
    printf("\n");
}*/

static bool abstraction_is_enabled(Solver* solver, CircuitAbstraction* abstraction, CircuitAbstraction* next) {
    const Circuit* circuit = solver->circuit;
    if (next->scope->node == 0) {
        // scopes in quantifier prefix are always enabled
        assert(abstraction->scope->num_next == 1);
        return true;
    }
    
    // a scope is enabled, if b_lit corresponding to scope_node is set to false
    const var_t scope_node_var = next->scope->node;
    const var_t scope_node_b_var = var_id_to_b_lit(circuit, scope_node_var);
    return int_vector_contains_sorted(abstraction->assumptions, scope_node_b_var);
}

qbf_res solve_recursive(Solver*, CircuitAbstraction* abstraction);

#ifdef PARALLEL_SOLVING
static qbf_res solve_sub(Solver* solver, CircuitAbstraction* next) {
    CircuitAbstraction* abstraction = next->prev;
    const bool is_existential = abstraction->scope->qtype == QUANT_EXISTS;
    const qbf_res good_result = is_existential ? QBF_RESULT_SAT : QBF_RESULT_UNSAT;
    const qbf_res bad_result = is_existential ? QBF_RESULT_UNSAT : QBF_RESULT_SAT;
    
    qbf_res sub_result = solve_recursive(solver, next);
    pthread_mutex_lock(&abstraction->mutex);
    if (sub_result == good_result) {
        circuit_abstraction_adjust_local_unsat_core(abstraction, next);
    } else {
        assert(sub_result == bad_result);
        refine(abstraction, next);
        abstraction->result = bad_result;
    }
    assert(abstraction->num_started > 0);
    abstraction->num_started--;
    if (abstraction->num_started == 0) {
        semaphore_post(&abstraction->sub_finished);
    }
    pthread_mutex_unlock(&abstraction->mutex);
    return sub_result;
}

static void* solve_sub_untyped(void* data) {
    struct solver_argument* arg = data;
    CircuitAbstraction* abs = arg->abstraction;
    Solver* solver = arg->solver;
    free(arg);
    solver_private* private = (solver_private*)solver;
    while (true) {
        semaphore_wait(&abs->has_entry);
        semaphore_wait(&private->num_threads);
        solve_sub(solver, abs);
        semaphore_post(&private->num_threads);
    }
    return NULL;
}

static void solve_sub_concurrently(Solver* solver, CircuitAbstraction* abstraction) {
    assert(abstraction->num_started == 0);
    abstraction->num_started++;
    
    solver_private* private = (solver_private*)solver;
    
    for (size_t i = 0; i < abstraction->scope->num_next; i++) {
        CircuitAbstraction* next = abstraction->next[i];
        assert(next != NULL);
        
        if (!abstraction_is_enabled(solver, abstraction, next)) {
            continue;
        }
        
        pthread_mutex_lock(&abstraction->mutex);
        abstraction->num_started++;
        pthread_mutex_unlock(&abstraction->mutex);
        if (abstraction->scope->num_next > 1) {
            semaphore_post(&next->has_entry);
        } else {
            solve_sub(solver, next);
        }
    }
    
    pthread_mutex_lock(&abstraction->mutex);
    abstraction->num_started--;
    if (abstraction->num_started == 0) {
        semaphore_post(&abstraction->sub_finished);
    }
    pthread_mutex_unlock(&abstraction->mutex);
    if (abstraction->scope->num_next > 1) {
        semaphore_wait(&abstraction->sub_finished);
    }
}
#else
static void solve_sub_sequentially(Solver* solver, CircuitAbstraction* abstraction) {
    const bool is_existential = abstraction->scope->qtype == QUANT_EXISTS;
    const qbf_res good_result = is_existential ? QBF_RESULT_SAT : QBF_RESULT_UNSAT;
    const qbf_res bad_result = is_existential ? QBF_RESULT_UNSAT : QBF_RESULT_SAT;
    
    for (size_t i = 0; i < abstraction->scope->num_next; i++) {
        CircuitAbstraction* next = abstraction->next[i];
        assert(next != NULL);
        
        if (!abstraction_is_enabled(solver, abstraction, next)) {
            continue;
        }
        
        qbf_res sub_result = solve_recursive(solver, next);
        if (sub_result == good_result) {
            circuit_abstraction_adjust_local_unsat_core(abstraction, next);
        } else {
            assert(sub_result == bad_result);
            refine(abstraction, next);
            abstraction->result = bad_result;
        }
    }
}
#endif

qbf_res solve_recursive(Solver* solver, CircuitAbstraction* abstraction) {
    const bool is_existential = abstraction->scope->qtype == QUANT_EXISTS;
    const qbf_res good_result = is_existential ? QBF_RESULT_SAT : QBF_RESULT_UNSAT;
    const qbf_res bad_result = is_existential ? QBF_RESULT_UNSAT : QBF_RESULT_SAT;
    //const solver_private* private = (solver_private*)solver;
    
    while (true) {
        logging_info("\n%s level %d\n", is_existential ? "existential" : "universal", abstraction->scope->scope_id);
        statistics_start_timer(abstraction->statistics);
        
        circuit_abstraction_assume_t_literals(abstraction, false);
        
        sat_res result = satsolver_sat(abstraction->sat);
        if (result == SATSOLVER_SATISFIABLE) {
            set_value_and_evaluate(solver, abstraction);
            
            if (abstraction->scope->num_next == 0) {
                circuit_abstraction_dual_propagation(abstraction);
                statistics_stop_and_record_timer(abstraction->statistics);
                return good_result;
            }
            assert(abstraction->scope->num_next > 0);
            
            circuit_abstraction_get_assumptions(abstraction);
            
            statistics_stop_and_record_timer(abstraction->statistics);
            
            abstraction->result = good_result;
            int_vector_reset(abstraction->local_unsat_core);
            
#ifdef PARALLEL_SOLVING
            solve_sub_concurrently(solver, abstraction);
#else
            solve_sub_sequentially(solver, abstraction);
#endif
            
            if (abstraction->result == good_result) {
                circuit_abstraction_dual_propagation(abstraction);
                return good_result;
            }
            
        } else {
            assert(result == SATSOLVER_UNSATISFIABLE);
            circuit_abstraction_get_unsat_core(abstraction);
            statistics_stop_and_record_timer(abstraction->statistics);
            return bad_result;
        }
    }
}


static void print_partial_assignment(Solver* solver, CircuitAbstraction* abstraction) {
    Circuit* circuit = solver->circuit;
    printf("V ");
    for (size_t i = 0; i < vector_count(circuit->vars); i++) {
        Var* var = vector_get(circuit->vars, i);
        if (var->scope != abstraction->scope) {
            continue;
        }
        if (var->shared.value == 0) {
            continue;
        }
        printf("%d ", var->shared.value > 0 ? var->shared.orig_id : -var->shared.orig_id);
    }
    printf("0\n");
}

static qbf_res solve(Solver* solver) {
    solver_private* private = (solver_private*)solver;
    qbf_res result = solve_recursive(solver, private->abstraction);
    bool partial_assignment = solver->options->partial_assignment;
    if (partial_assignment) {
        CircuitAbstraction* top_level = private->abstraction;
        if (vector_count(top_level->scope->vars) == 0 && top_level->scope->num_next == 1) {
            top_level = top_level->next[0];
        }
        if ((result == QBF_RESULT_SAT && top_level->scope->qtype == QUANT_EXISTS)
            || (result == QBF_RESULT_UNSAT && top_level->scope->qtype == QUANT_FORALL)) {
            print_partial_assignment(solver, top_level);
        }
    }
    return result;
}

// Interface implementation
Solver* solver_init(SolverOptions* options, Circuit* circuit) {
    solver_private* private = malloc(sizeof(solver_private));
    private->public.options = options;
    private->public.circuit = circuit;
    private->abstraction = NULL;
    
#ifdef PARALLEL_SOLVING
    semaphore_init(&private->num_threads, options->num_threads);
#endif
    
#ifdef CERTIFICATION
    if (options->certify) {
        certification_init(&private->public.cert, circuit);
    }
#endif
    
    // Statistics
    private->encoding = statistics_init(10000);
    private->preprocessing = statistics_init(10000);
    private->building_abstraction = statistics_init(10000);
    private->solving = statistics_init(10000);
    return &private->public;
}

void solver_free(Solver* solver) {
    solver_private* private = (solver_private*)solver;
    circuit_abstraction_free_recursive(private->abstraction);
    
    statistics_free(private->encoding);
    statistics_free(private->preprocessing);
    statistics_free(private->building_abstraction);
    statistics_free(private->solving);
    free(private);
}

SolverOptions* solver_get_default_options() {
    SolverOptions* options = malloc(sizeof(SolverOptions));
    options->preprocess = true;
    options->miniscoping = false;
    options->certify = false;
    options->statistics = false;
    options->partial_assignment = false;
    
    // low level solver features
    options->assignment_b_lit_minimization = true;  // minimize b_lit entry according to assignments of circuit
    options->use_partial_deref = false;
    options->use_combined_abstraction = true;
    
#ifdef PARALLEL_SOLVING
    options->num_threads = 2;
    options->assignment_b_lit_minimization = false;
#endif
    return options;
}

qbf_res solver_sat(Solver* solver) {
    solver_private* private = (solver_private*)solver;
    statistics_start_timer(private->encoding);
    circuit_reencode(solver->circuit);
    statistics_stop_and_record_timer(private->encoding);
    
    if (solver->options->preprocess) {
        statistics_start_timer(private->preprocessing);
        circuit_preprocess(solver->circuit);
        statistics_stop_and_record_timer(private->preprocessing);
    }
    
    if (solver->options->miniscoping) {
        circuit_unprenex_by_miniscoping(solver->circuit);
        // TODO: preprocess after miniscoping?
    }
    
    if (logging_get_verbosity() >= VERBOSITY_ALL) {
        circuit_print_qcir(solver->circuit);
        circuit_print_dot(solver->circuit);
        circuit_print_qdimacs(solver->circuit);
    }
    
#ifdef CERTIFICATION
    if (solver->options->certify) {
        certification_import_variables(&solver->cert, solver->circuit);
    }
#endif
    
    
    statistics_start_timer(private->building_abstraction);
    circuit_compute_scope_influence(solver->circuit);
    if (!circuit_is_prenex(solver->circuit)) {
        logging_warn("The input appears to be non-prenex. Solving is supported but is less tested than prenex input. Try prenexing the formula if you encounter problems.\n");
        circuit_compute_relevant_scopes(solver->circuit);
    }
    
    private->abstraction = build_circuit_abstraction(solver, solver->circuit->top_level, NULL);
    statistics_stop_and_record_timer(private->building_abstraction);
    
    statistics_start_timer(private->solving);
    qbf_res result = solve(solver);
    statistics_stop_and_record_timer(private->solving);
    
#ifdef CERTIFICATION
    if (solver->options->certify) {
        certification_define_outputs(&solver->cert, private->abstraction, result);
    }
#endif
    
    return result;
}

static void print_scope_statistics_recursively(CircuitAbstraction* abs) {
    printf("Statistics for %s level %d\n", abs->scope->qtype == QUANT_EXISTS ? "existential" : "universal", abs->scope->scope_id);
    statistics_print(abs->statistics);
    
    for (size_t i = 0; i < abs->scope->num_next; i++) {
        print_scope_statistics_recursively(abs->next[i]);
    }
}

void solver_print_statistics(Solver* solver) {
    solver_private* private = (solver_private*)solver;
    
    printf("Reencoding of circuit took ");
    statistics_print_time(private->encoding);
    
    printf("Preprocessing took ");
    statistics_print_time(private->preprocessing);
    
    printf("Building abstraction took ");
    statistics_print_time(private->building_abstraction);
    
    printf("Solving took ");
    statistics_print_time(private->solving);
    
    printf("\nDetailed solving statistics:\n");
    print_scope_statistics_recursively(private->abstraction);
}
