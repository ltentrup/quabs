//
//  fixpoint.c
//  caqe-qcir
//
//  Created by Leander Tentrup on 26.04.16.
//  Copyright © 2016 Saarland University. All rights reserved.
//

#include "fixpoint.h"

#include <string.h>

#include "circuit_print.h"
#include "logging.h"

/**
 * refinement_data describes the information needed to check whether a
 * refinement is still valid in later iterations:
 * - incremental_literal is the literal used in the SAT solver, i.e., clause (-incremental_literal [literals]*)
 * - sub_states is a int_vector that described the states for which this refinement is correct
 */
typedef struct {
    lit_t incremental_literal;
    int_vector* sub_states;
} refinement_data;

static lit_t aig_lit_to_qcir_lit(Fixpoint* fixpoint, unsigned aig_lit) {
    assert(fixpoint->constant_zero != 0);
    if (aig_lit == 0) {
        return (lit_t)fixpoint->constant_zero;
    } else if (aig_lit == 1) {
        return -(lit_t)fixpoint->constant_zero;
    } else if (aig_lit % 2) {
        return -((aig_lit - 1) / 2);
    } else {
        return aig_lit / 2;
    }
}

/**
 * Imports an AIGER circuit into the Circuit data structure.
 * The following conversion is assumed: for an AIGER lit aig_lit,
 * the corresponding Circuit lit is aig_lit/2.
 */
static void import_aiger_circuit(Fixpoint* fixpoint) {
    for (unsigned i = 0; i < fixpoint->instance->num_ands; i++) {
        aiger_and and = fixpoint->instance->ands[i];
        Gate* gate = circuit_add_gate(fixpoint->circuit, aig_lit_to_qcir_lit(fixpoint, and.lhs), GATE_AND);
        circuit_add_to_gate(fixpoint->circuit, gate, aig_lit_to_qcir_lit(fixpoint, and.rhs0));
        circuit_add_to_gate(fixpoint->circuit, gate, aig_lit_to_qcir_lit(fixpoint, and.rhs1));
    }
}

static lit_t prime_latch(aiger* aiger, lit_t latch_id) {
    return latch_id + aiger->maxvar + 2;
}

static void import_aiger_variables(Fixpoint* fixpoint) {
    Circuit* circuit = fixpoint->circuit;
    aiger* instance = fixpoint->instance;
    
    assert(circuit->top_level->num_next == 1);
    Scope* outer = circuit->top_level->next[0];
    assert(outer != NULL);
    assert(outer->qtype == QUANT_FORALL);
    
    assert(outer->num_next == 1);
    Scope* inner = outer->next[0];
    assert(inner != NULL);
    assert(inner->qtype == QUANT_EXISTS);
    
    for (size_t i = 0; i < instance->num_inputs; i++) {
        aiger_symbol sym = instance->inputs[i];
        if (sym.name && strncmp(sym.name, "controllable_", 13) == 0) {
            // controllable input
            circuit_new_var(circuit, inner, aig_lit_to_qcir_lit(fixpoint, sym.lit));
        } else {
            // uncontrollable input
            circuit_new_var(circuit, outer, aig_lit_to_qcir_lit(fixpoint, sym.lit));
        }
    }
    
    for (size_t i = 0; i < instance->num_latches; i++) {
        aiger_symbol sym = instance->latches[i];
        int lit = aig_lit_to_qcir_lit(fixpoint, sym.lit);
        Var* var = circuit_new_var(circuit, outer, lit);
        int primed_lit = prime_latch(instance, i);
        Var* primed = circuit_new_var(circuit, inner, primed_lit);
        map_add(fixpoint->prime_mapping, lit, primed);
        map_add(fixpoint->unprime_mapping, primed_lit, var);
    }
}

static void build_equality(Circuit* circuit, Gate* and_gate, int lhs, int rhs) {
    // lhs -> rhs
    Gate* imp_pos = circuit_add_gate(circuit, circuit->max_num + 1, GATE_OR);
    circuit_add_to_gate(circuit, and_gate, imp_pos->shared.id);
    circuit_add_to_gate(circuit, imp_pos, -lhs);
    circuit_add_to_gate(circuit, imp_pos, rhs);
    
    // !lhs -> !rhs
    Gate* imp_neg = circuit_add_gate(circuit, circuit->max_num + 1, GATE_OR);
    circuit_add_to_gate(circuit, and_gate, imp_neg->shared.id);
    circuit_add_to_gate(circuit, imp_neg, lhs);
    circuit_add_to_gate(circuit, imp_neg, -rhs);
}

/**
 * Build the query F -> (T /\ F') where
 * - F = true
 * - T = /\_l (l' <-> f_l) /\ ~bad
 */
static void build_qbf_query(Fixpoint* fixpoint) {
    Circuit* circuit = fixpoint->circuit;
    aiger* instance = fixpoint->instance;
    
    int implication_lit = circuit->max_num + 1;
    Gate* implication_gate = circuit_add_gate(circuit, implication_lit, GATE_OR);
    circuit_set_output(circuit, implication_lit);
    //circuit_add_to_gate(circuit, implication_gate, -aig_lit_to_qcir_lit(fixpoint, instance->outputs[0].lit));
    
    // F
    Gate* f = circuit_add_gate(circuit, circuit->max_num + 1, GATE_OR);
    circuit_add_to_gate(circuit, f, aig_lit_to_qcir_lit(fixpoint, instance->latches[instance->num_latches-1].lit));
    f->keep = true;
    circuit_add_to_gate(circuit, implication_gate, f->shared.id);
    fixpoint->fixpoint = f;
    //circuit_add_to_gate(circuit, implication_gate, aig_lit_to_qcir_lit(fixpoint, instance->latches[instance->num_latches-1].lit));
    
    // T
    int transition_lit = circuit->max_num + 1;
    Gate* transition_gate = circuit_add_gate(circuit, transition_lit, GATE_AND);
    circuit_add_to_gate(circuit, implication_gate, transition_lit);
    
    circuit_add_to_gate(circuit, transition_gate, -aig_lit_to_qcir_lit(fixpoint, instance->outputs[0].lit));
    for (size_t i = 0; i < instance->num_latches - 1; i++) {
        aiger_symbol sym = instance->latches[i];
        if (sym.next == 1) {
            circuit_add_to_gate(circuit, transition_gate, prime_latch(instance, i));
        } else {
            build_equality(circuit, transition_gate, prime_latch(instance, i), aig_lit_to_qcir_lit(fixpoint, sym.next));
        }
    }
    
    // F'
    Gate* f_prime = circuit_add_gate(circuit, circuit->max_num + 1, GATE_AND);
    circuit_add_to_gate(circuit, f_prime, -prime_latch(instance, instance->num_latches-1));
    f_prime->keep = true;
    circuit_add_to_gate(circuit, transition_gate, f_prime->shared.id);
    //circuit_add_to_gate(circuit, transition_gate, -prime_latch(instance, instance->num_latches-1));
    fixpoint->fixpoint_prime = f_prime;
}

Fixpoint* fixpoint_init_from_aiger(aiger* instance) {
    Fixpoint* fixpoint = malloc(sizeof(Fixpoint));
    
    fixpoint->instance = instance;
    fixpoint->circuit = circuit_init();
    
    fixpoint->universal = NULL;
    fixpoint->existential = NULL;
    
    fixpoint->options = solver_get_default_options();
    fixpoint->options->preprocess = false;
    fixpoint->options->assignment_b_lit_minimization = true;
    fixpoint->options->use_combined_abstraction = true;
    
    fixpoint->prime_mapping = map_init();
    fixpoint->unprime_mapping = map_init();
    
    // incremental helper
    int_queue_init(&fixpoint->universal_incremental);
    int_queue_init(&fixpoint->existential_incremental);
    fixpoint->incremental_lit = 0;
    fixpoint->next_lit = 0;
    fixpoint->refinements = vector_init();
    fixpoint->excluded_states = int_vector_init();
    
    // Statistics
    fixpoint->num_refinements = statistics_init(10000);
    fixpoint->exclusions = statistics_init(10000);
    fixpoint->cube_size = statistics_init(1);
    fixpoint->outer_sat = statistics_init(10000);
    fixpoint->inner_sat = statistics_init(10000);
    fixpoint->sat_refinements = statistics_init(10000);
    fixpoint->unsat_refinements = statistics_init(10000);
    
    // add fake error latch, takes the single output as input
    aiger_add_latch(instance, (instance->maxvar + 1) * 2, instance->outputs[0].lit, "fake error latch");
    
    // add constant zero
    fixpoint->constant_zero = instance->maxvar + 1;
    circuit_add_gate(fixpoint->circuit, fixpoint->constant_zero, GATE_OR);
    
    Scope* outer = circuit_init_scope(fixpoint->circuit, QUANT_FORALL);
    assert(outer);
    Scope* inner = circuit_init_scope(fixpoint->circuit, QUANT_EXISTS);
    assert(inner);
    
    import_aiger_variables(fixpoint);
    import_aiger_circuit(fixpoint);
    
    build_qbf_query(fixpoint);
    
    circuit_reencode(fixpoint->circuit);
    
    // first literal that is not used in any SAT encoding
    fixpoint->next_lit = (2 * fixpoint->circuit->max_num) + 1;
    
    return fixpoint;
}

static void push_incremental(Fixpoint* fixpoint) {
    assert(fixpoint->incremental_lit == 0);
    fixpoint->incremental_lit = fixpoint->next_lit++;
    
    satsolver_add(fixpoint->universal->negation, -fixpoint->incremental_lit);
    satsolver_add(fixpoint->universal->negation, -(lit_t)fixpoint->fixpoint->shared.id);
    for (int_queue_element* iterator = fixpoint->universal_incremental.first; iterator != NULL; iterator = iterator->next) {
        satsolver_add(fixpoint->universal->negation, iterator->value);
    }
    satsolver_add(fixpoint->universal->negation, 0);
    
    satsolver_add(fixpoint->existential->negation, -fixpoint->incremental_lit);
    satsolver_add(fixpoint->existential->negation, -(lit_t)fixpoint->fixpoint_prime->shared.id);
    for (int_queue_element* iterator = fixpoint->existential_incremental.first; iterator != NULL; iterator = iterator->next) {
        satsolver_add(fixpoint->existential->negation, iterator->value);
    }
    satsolver_add(fixpoint->existential->negation, 0);
}

static void init_incremental(Fixpoint* fixpoint) {
    // F in dual solver of universal quantifier is build incremental
    assert(fixpoint->fixpoint->num_inputs == 1);
    //lit_t input = fixpoint->fixpoint->inputs[0];
    //assert(input > 0);
    //Var* var = fixpoint->circuit->nodes[lit_to_var(input)];
    //int_queue_push(&fixpoint->universal_incremental, var->shared.id);
    
    // F' in dual solver of existential quantifier is build incremental
    assert(fixpoint->fixpoint_prime->num_inputs == 1);
    //input = fixpoint->fixpoint_prime->inputs[0];
    //assert(input < 0);
    //var = fixpoint->circuit->nodes[lit_to_var(input)];
    //int_queue_push(&fixpoint->existential_incremental, var->shared.id);
    
    push_incremental(fixpoint);
}

static void setup(Fixpoint* fixpoint) {
    Circuit* circuit = fixpoint->circuit;
    
    if (logging_get_verbosity() >= VERBOSITY_ALL) {
        circuit_print_qcir(circuit);
    }
    
    circuit_compute_scope_influence(circuit);
    
    assert(circuit->top_level->num_next == 1);
    Scope* universal = circuit->top_level->next[0];
    fixpoint->universal = circuit_abstraction_init(fixpoint->options, NULL, universal, NULL);
    
    assert(universal->num_next == 1);
    Scope* existential = universal->next[0];
    assert(existential->num_next == 0);
    fixpoint->existential = circuit_abstraction_init(fixpoint->options, NULL, existential, fixpoint->universal);
    fixpoint->universal->next[0] = fixpoint->existential;
    
    init_incremental(fixpoint);
}

static void get_assignments_from_sat_query(CircuitAbstraction* abstraction) {
    for (size_t i = 0; i < vector_count(abstraction->scope->vars); i++) {
        const Var* var = vector_get(abstraction->scope->vars, i);
        const lit_t sat_var = create_lit(var->shared.id, false);
        const int value = satsolver_deref(abstraction->sat, sat_var);
        //circuit_set_value(abstraction->scope->circuit, var->shared.id, value);
        circuit_set_value(abstraction->scope->circuit, var->shared.id, value * abstraction->scope->scope_id);
        logging_debug("%d ", create_lit_from_value(var->shared.id, value));
    }
    logging_debug("\n");
    
    circuit_evaluate_max(abstraction->scope->circuit, abstraction->scope->scope_id);

}

static refinement_data* init_refinement(Fixpoint* fixpoint) {
    refinement_data* data = malloc(sizeof(refinement_data));
    data->incremental_literal = fixpoint->next_lit++;
    data->sub_states = int_vector_init();
    return data;
}

static void refine(Fixpoint* fixpoint, CircuitAbstraction* abstraction, CircuitAbstraction* child) {
    Circuit* circuit = abstraction->scope->circuit;
    
    refinement_data* data = init_refinement(fixpoint);
    vector_add(fixpoint->refinements, data);
    
    logging_debug("%d: ", data->incremental_literal);
    
    for (size_t i = 0; i < vector_count(child->scope->vars); i++) {
        Var* primed_var = vector_get(child->scope->vars, i);
        if (primed_var->shared.value == 0) {
            continue;
        }
        Var* var = map_get(fixpoint->unprime_mapping, primed_var->shared.orig_id);
        if (var == NULL) {
            // not a latch
            assert(aiger_lit2tag(fixpoint->instance, primed_var->shared.orig_id * 2) != 2);
            continue;
        }
        lit_t state_value = create_lit_from_value(var->shared.id, primed_var->shared.value);
        int_vector_add(data->sub_states, state_value);
        logging_debug("%d ", state_value);
    }
    logging_debug("\n");
    
    // incremental variable
    assert(data->incremental_literal != 0);
    satsolver_add(abstraction->sat, -data->incremental_literal);
    
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
    satsolver_add(abstraction->sat, 0);
    logging_debug("\n");
}

static void check_old_refinements(Fixpoint* fixpoint) {
    for (size_t i = 0; i < vector_count(fixpoint->refinements); i++) {
        refinement_data* data = vector_get(fixpoint->refinements, i);
        bool subset = true;
        for (size_t j = 0; j < int_vector_count(fixpoint->excluded_states); j++) {
            lit_t state_lit = int_vector_get(fixpoint->excluded_states, j);
            if (int_vector_find(data->sub_states, state_lit) == VECTOR_NOT_FOUND) {
                subset = false;
                break;
            }
        }
        if (subset) {
            logging_debug("refinement %d is no longer valid\n", data->incremental_literal);
            
            satsolver_add(fixpoint->universal->sat, -data->incremental_literal);
            satsolver_add(fixpoint->universal->sat, 0);
            satsolver_add(fixpoint->universal->negation, -data->incremental_literal);
            satsolver_add(fixpoint->universal->negation, 0);
            
            int_vector_free(data->sub_states);
            free(data);
            vector_set(fixpoint->refinements, i, NULL);
        }
    }
    vector_compress(fixpoint->refinements);
}

static void assume_refinements(Fixpoint* fixpoint, bool negation) {
    SATSolver* sat = negation ? fixpoint->universal->negation : fixpoint->universal->sat;
    for (size_t i = 0; i < vector_count(fixpoint->refinements); i++) {
        refinement_data* data = vector_get(fixpoint->refinements, i);
        satsolver_assume(sat, data->incremental_literal);
    }
}

static fixpoint_res exclude_states(Fixpoint* fixpoint) {
    logging_info("# ");
    bool initial_state_contained = true;
    
    
    // Remove old incremental variable
    assert(fixpoint->incremental_lit != 0);
    
    satsolver_add(fixpoint->existential->sat, -fixpoint->incremental_lit);
    satsolver_add(fixpoint->existential->sat, 0);
    satsolver_add(fixpoint->existential->negation, -fixpoint->incremental_lit);
    satsolver_add(fixpoint->existential->negation, 0);
    
    satsolver_add(fixpoint->universal->sat, -fixpoint->incremental_lit);
    satsolver_add(fixpoint->universal->sat, 0);
    satsolver_add(fixpoint->universal->negation, -fixpoint->incremental_lit);
    satsolver_add(fixpoint->universal->negation, 0);
    
    fixpoint->incremental_lit = 0;
    
    
    //satsolver_pop(fixpoint->universal->sat);
    //satsolver_pop(fixpoint->existential->sat);
    
    //satsolver_pop(fixpoint->universal->negation);
    //satsolver_pop(fixpoint->existential->negation);
    const lit_t next_universal_lit = fixpoint->next_lit++; //satsolver_new_lit(fixpoint->universal->negation);
    const lit_t next_existential_lit = next_universal_lit;
    int_queue_push(&fixpoint->universal_incremental, next_universal_lit);
    int_queue_push(&fixpoint->existential_incremental, next_existential_lit);
    
    int_vector_reset(fixpoint->excluded_states);
    unsigned cube_size = 0;
    for (unsigned i = 0; i < vector_count(fixpoint->universal->scope->vars); i++) {
        Var* var = vector_get(fixpoint->universal->scope->vars, i);
        if (aiger_lit2tag(fixpoint->instance, var->shared.orig_id * 2) != 2) {
            // We are only interested in latch values
            continue;
        }
        if (var->shared.value == 0) {
            continue;
        }
        cube_size++;
        
        lit_t state_lit = create_lit_from_value(var->shared.id, var->shared.value);
        int_vector_add(fixpoint->excluded_states, state_lit);
        logging_info("%d ", state_lit);
        satsolver_add(fixpoint->universal->sat, -state_lit);
        
        satsolver_add(fixpoint->universal->negation, -next_universal_lit);
        satsolver_add(fixpoint->universal->negation, state_lit);
        satsolver_add(fixpoint->universal->negation, 0);
        
        Var* primed = map_get(fixpoint->prime_mapping, var->shared.orig_id);
        assert(primed != NULL);
        lit_t primed_state_lit = var->shared.value < 0 ? -primed->shared.id : primed->shared.id;
        satsolver_add(fixpoint->existential->sat, -primed_state_lit);
        
        satsolver_add(fixpoint->existential->negation, -next_existential_lit);
        satsolver_add(fixpoint->existential->negation, primed_state_lit);
        satsolver_add(fixpoint->existential->negation, 0);
        
        if (state_lit > 0) {
            initial_state_contained = false;
        }
    }
    satsolver_add(fixpoint->universal->sat, 0);
    satsolver_add(fixpoint->existential->sat, 0);
    logging_info("\n");
    statistic_add_value(fixpoint->cube_size, cube_size);
    
    // check prior refinements
    check_old_refinements(fixpoint);
    
    if (initial_state_contained) {
        return FIXPOINT_NO_FIXPOINT;
    }
    
    push_incremental(fixpoint);
    
    return FIXPOINT_NO_RESULT;
}

static bool encodes_latch(Fixpoint* fixpoint, const Var* var, bool universal) {
    if (universal && aiger_lit2tag(fixpoint->instance, var->shared.orig_id * 2) != 2) {
        // not a latch
        return false;
    } else if (!universal && map_get(fixpoint->unprime_mapping, var->shared.orig_id) == NULL) {
        // not a primed latch
        return false;
    }
    return true;
}

static void assume_assignment(Fixpoint* fixpoint, bool universal) {
    CircuitAbstraction* abstraction = universal ? fixpoint->universal : fixpoint->existential;
    
    // Assume variables first ...
    for (size_t i = 0; i < vector_count(abstraction->scope->vars); i++) {
        const Var* var = vector_get(abstraction->scope->vars, i);
        if (encodes_latch(fixpoint, var, universal)) {
            continue;
        }
        
        const lit_t sat_var = create_lit(var->shared.id, false);
        const int value = satsolver_deref(abstraction->sat, sat_var);
        const lit_t sat_lit = create_lit_from_value(sat_var, value);
        if (value != 0) {
            logging_debug("%d ", sat_lit);
            satsolver_assume(abstraction->negation, sat_lit);
        }
    }
    
    // ... then latch values
    for (size_t i = 0; i < vector_count(abstraction->scope->vars); i++) {
        const Var* var = vector_get(abstraction->scope->vars, i);
        if (!encodes_latch(fixpoint, var, universal)) {
            continue;
        }
        
        const lit_t sat_var = create_lit(var->shared.id, false);
        const int value = satsolver_deref(abstraction->sat, sat_var);
        const lit_t sat_lit = create_lit_from_value(sat_var, value);
        if (value != 0) {
            logging_debug("%d ", sat_lit);
            satsolver_assume(abstraction->negation, sat_lit);
        }
    }
    
    logging_debug("\n");
}

static void assume_failed_t_literals(CircuitAbstraction* abstraction, bool negation) {
    for (size_t i = 0; i < int_vector_count(abstraction->t_lits); i++) {
        lit_t t_lit = int_vector_get(abstraction->t_lits, i);
        const var_t var_id = t_lit_to_var_id(abstraction->scope->circuit, t_lit);
        if (bit_vector_contains(abstraction->entry, t_lit)) {
            t_lit = -t_lit;
        }
        if (negation) {
            satsolver_assume(abstraction->negation, -t_lit);
        } else {
            satsolver_assume(abstraction->sat, t_lit);
        }
        logging_debug("t%d ", create_lit_from_value(var_id, t_lit));
    }
    logging_debug("\n");
}

static void minimize_by_dropping_literals(Fixpoint* fixpoint, bool failed) {
    CircuitAbstraction* abstraction = fixpoint->existential;
    for (size_t t_lit = bit_vector_init_iteration(abstraction->entry); bit_vector_iterate(abstraction->entry); t_lit = bit_vector_next(abstraction->entry)) {
        bit_vector_remove(abstraction->entry, t_lit);
        satsolver_assume(abstraction->sat, fixpoint->incremental_lit);
        if (failed) {
            assume_failed_t_literals(abstraction, false);
        } else {
            circuit_abstraction_assume_t_literals(abstraction, false);
        }
        sat_res result = failed ? SATSOLVER_RESULT_SAT : SATSOLVER_RESULT_UNSAT;
        if (satsolver_sat(abstraction->sat) == result) {
            bit_vector_add(abstraction->entry, t_lit);
        } else {
            //abort();
        }
    }
}

static void minimize_by_dropping_variables(Fixpoint* fixpoint, bool universal) {
    CircuitAbstraction* abstraction = universal ? fixpoint->universal : fixpoint->existential;
    for (unsigned i = 0; i < vector_count(abstraction->scope->vars); i++) {
        Var* candidate = vector_get(abstraction->scope->vars, i);
        if (candidate->shared.value == 0) {
            continue;
        }
        if (!encodes_latch(fixpoint, candidate, universal)) {
            continue;
        }
        for (unsigned j = 0; j < vector_count(abstraction->scope->vars); j++) {
            Var* var = vector_get(abstraction->scope->vars, j);
            if (var->shared.value == 0) {
                continue;
            }
            if (var == candidate) {
                continue;
            }
            satsolver_assume(abstraction->negation, create_lit_from_value(var->shared.id, var->shared.value));
        }
        satsolver_assume(abstraction->negation, fixpoint->incremental_lit);
        circuit_abstraction_assume_t_literals(abstraction, true);
        if (satsolver_sat(abstraction->negation) == SATSOLVER_RESULT_UNSAT) {
            candidate->shared.value = 0;
        }
    }
}

static void fixpoint_dual_propagation(Fixpoint* fixpoint, bool universal) {
    CircuitAbstraction* abstraction = universal ? fixpoint->universal : fixpoint->existential;
    Circuit* circuit = abstraction->scope->circuit;
    
    assume_assignment(fixpoint, universal);
    satsolver_assume(abstraction->negation, fixpoint->incremental_lit);
    circuit_abstraction_assume_t_literals(abstraction, true);
    
    if (abstraction->scope->num_next != 0) {
        // Add UNSAT core as blocking clause
        for (size_t i = 0; i < int_vector_count(abstraction->local_unsat_core); i++) {
            const lit_t failed_t_lit = int_vector_get(abstraction->local_unsat_core, i);
            assert(failed_t_lit > 0);
            const lit_t b_lit = t_lit_to_b_lit(circuit, failed_t_lit);
            
            logging_debug("b%d ", t_lit_to_var_id(circuit, failed_t_lit));
            
            if (!int_vector_contains_sorted(abstraction->b_lits, b_lit)) {
                satsolver_add(abstraction->negation, failed_t_lit);
                continue;
            }
            satsolver_add(abstraction->negation, b_lit);
        }
        satsolver_add(abstraction->negation, 0);
    }
    logging_debug("\n");
    
    sat_res result = satsolver_sat(abstraction->negation);
    assert(result == SATSOLVER_UNSATISFIABLE);
    if (result != SATSOLVER_UNSATISFIABLE) {
        abort();
    }
    
    // Reset variable assignments
    for (size_t i = 0; i < vector_count(abstraction->scope->vars); i++) {
        Var* var = vector_get(abstraction->scope->vars, i);
        if (!encodes_latch(fixpoint, var, universal)) {
            continue;
        }
        var->shared.value = 0;
    }
    
    bit_vector_reset(abstraction->entry);
    const int* failed_assumptions = satsolver_failed_assumptions(abstraction->negation);
    logging_debug("min ");
    for (size_t i = 0; failed_assumptions[i] != 0; i++) {
        const lit_t failed_lit = failed_assumptions[i];  // may be variable or t-lit
        const var_t failed_var = lit_to_var(failed_lit);
        if (failed_var <= circuit->max_num) {
            assert(circuit->types[failed_var] == NODE_VAR);
            
            // variable assignments
            Var* var = circuit->nodes[failed_var];
            if (!encodes_latch(fixpoint, var, universal)) {
                continue;
            }
            int value = create_lit_from_value(abstraction->scope->scope_id, failed_lit);
            circuit_set_value(circuit, var->shared.id, value);
            continue;
        }
        if (failed_lit > 0) {
            // variable used for incremental push/pop
            continue;
        }
        const lit_t failed_t_lit = -failed_lit;
        assert(failed_t_lit > 0);
        
        const var_t node = t_lit_to_var_id(circuit, failed_t_lit);
        logging_debug("t%d ", node);
        
        bit_vector_add(abstraction->entry, failed_t_lit);
    }
    logging_debug("\n");
}

fixpoint_res fixpoint_solve(Fixpoint* fixpoint) {
    
    setup(fixpoint);
    
    statistics_start_timer(fixpoint->exclusions);
    
    while (true) {
        statistics_start_timer(fixpoint->num_refinements);
        statistics_start_timer(fixpoint->outer_sat);
        assert(fixpoint->incremental_lit != 0);
        satsolver_assume(fixpoint->universal->sat, fixpoint->incremental_lit);
        assume_refinements(fixpoint, false);
        sat_res result = satsolver_sat(fixpoint->universal->sat);
        if (result == SATSOLVER_RESULT_UNSAT) {
            statistics_stop_and_record_timer(fixpoint->outer_sat);
            return FIXPOINT_INDUCTIVE;
        }
        get_assignments_from_sat_query(fixpoint->universal);
        circuit_abstraction_get_assumptions(fixpoint->universal);
        statistics_stop_and_record_timer(fixpoint->outer_sat);
        
        statistics_start_timer(fixpoint->inner_sat);
        circuit_abstraction_assume_t_literals(fixpoint->existential, false);
        satsolver_assume(fixpoint->existential->sat, fixpoint->incremental_lit);
        result = satsolver_sat(fixpoint->existential->sat);
        if (result == SATSOLVER_RESULT_UNSAT) {
            circuit_abstraction_get_unsat_core(fixpoint->existential);
            minimize_by_dropping_literals(fixpoint, true);
            circuit_abstraction_adjust_local_unsat_core(fixpoint->universal, fixpoint->existential);
            statistics_stop_and_record_timer(fixpoint->inner_sat);
            
            // dual abstraction adjustment
            statistics_start_timer(fixpoint->unsat_refinements);
            satsolver_add(fixpoint->universal->negation, -fixpoint->incremental_lit);
            fixpoint_dual_propagation(fixpoint, true);
            
            minimize_by_dropping_variables(fixpoint, true);
            
            fixpoint_res excluded = exclude_states(fixpoint);
            int_vector_reset(fixpoint->universal->local_unsat_core);
            statistics_stop_and_record_timer(fixpoint->unsat_refinements);
            statistics_stop_and_record_timer(fixpoint->exclusions);
            if (excluded == FIXPOINT_NO_FIXPOINT) {
                return FIXPOINT_NO_FIXPOINT;
            }
            statistics_start_timer(fixpoint->exclusions);
            continue;
        }
        get_assignments_from_sat_query(fixpoint->existential);
        statistics_stop_and_record_timer(fixpoint->inner_sat);
        
        // dual abstraction adjustments
        statistics_start_timer(fixpoint->sat_refinements);
        minimize_by_dropping_literals(fixpoint, false);
        circuit_abstraction_assume_t_literals(fixpoint->existential, false);
        satsolver_assume(fixpoint->existential->sat, fixpoint->incremental_lit);
        result = satsolver_sat(fixpoint->existential->sat);
        assert(result == SATSOLVER_RESULT_SAT);
        fixpoint_dual_propagation(fixpoint, false);
        
        //minimize_by_dropping_variables(fixpoint, false);
        
        refine(fixpoint, fixpoint->universal, fixpoint->existential);
        statistics_stop_and_record_timer(fixpoint->sat_refinements);
        statistics_stop_and_record_timer(fixpoint->num_refinements);
    }
    
    return FIXPOINT_NO_RESULT;
}


void fixpoint_aiger_preprocess(aiger* aig) {
    // make sure that aiger instance is topological sorted
    if (!aiger_is_reencoded(aig)) {
        aiger_reencode(aig);
    }
    map* constants = map_init(); // maps and gate if its output is constant
    for (unsigned i = 0; i < aig->num_ands; i++) {
        aiger_and and = aig->ands[i];
        bool changed = false;
        
        const unsigned rhs0_var = aiger_strip(and.rhs0);
        if (map_contains(constants, rhs0_var)) {
            unsigned new_rhs0 = (unsigned)map_get(constants, rhs0_var);
            if (aiger_sign(and.rhs0) == 1) {
                new_rhs0 = aiger_not(new_rhs0);
            }
            and.rhs0 = new_rhs0;
            changed = true;
        }
        const unsigned rhs1_var = aiger_strip(and.rhs1);
        if (map_contains(constants, rhs1_var)) {
            unsigned new_rhs1 = (unsigned)map_get(constants, rhs1_var);
            if (aiger_sign(and.rhs1) == 1) {
                new_rhs1 = aiger_not(new_rhs1);
            }
            and.rhs1 = new_rhs1;
            changed = true;
        }
        
        if (and.rhs0 == 1) {
            map_add(constants, and.lhs, (void*)(uintptr_t)and.rhs1);
        } else if (and.rhs1 == 1) {
            map_add(constants, and.lhs, (void*)(uintptr_t)and.rhs0);
        }
        
        if (changed) {
            aig->ands[i] = and;
        }
    }
    for (unsigned i = 0; i < aig->num_latches; i++) {
        aiger_symbol latch = aig->latches[i];
        const unsigned next_var = aiger_strip(latch.next);
        if (map_contains(constants, next_var)) {
            unsigned new_next = (unsigned)map_get(constants, next_var);
            if (aiger_sign(latch.next) == 1) {
                new_next = aiger_not(new_next);
            }
            latch.next = new_next;
            aig->latches[i] = latch;
        }
    }
    map_free(constants);
    aiger_reencode(aig);
}

