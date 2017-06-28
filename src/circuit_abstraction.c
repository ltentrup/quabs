//
//  circuit_abstraction.c
//  caqe-qcir
//
//  Created by Leander Tentrup on 03.07.15.
//  Copyright (c) 2015 Saarland University. All rights reserved.
//

#include <assert.h>

#include "circuit_abstraction.h"
#include "solver.h"
#include "circuit.h"
#include "util.h"
#include "logging.h"

#ifdef CERTIFICATION
#include "certification.h"
#endif

var_t node_to_t_lit(const Circuit* circuit, const node_shared* node) {
    return var_id_to_t_lit(circuit, node->id);
}

var_t var_id_to_t_lit(const Circuit* circuit, var_t var_id) {
    return var_id + circuit->max_num;
}

var_t node_to_b_lit(const Circuit* circuit, const node_shared* node) {
    return var_id_to_b_lit(circuit, node->id);
}

var_t var_id_to_b_lit(const Circuit* circuit, var_t var_id) {
    assert(circuit);
    return var_id;
    //var_t t_lit = var_id_to_t_lit(circuit, var_id);
    //return t_lit_to_b_lit(circuit, t_lit);
}

var_t t_lit_to_b_lit(const Circuit* circuit, var_t t_lit) {
    return t_lit_to_var_id(circuit, t_lit);
    //return t_lit + circuit->max_num;
}

var_t t_lit_to_var_id(const Circuit* circuit, var_t t_lit) {
    assert(t_lit > circuit->max_num);
    return t_lit - circuit->max_num;
}

var_t b_lit_to_var_id(const Circuit* circuit, var_t b_lit) {
    assert(circuit);
    return b_lit;
    //return b_lit - 2 * circuit->max_num;
}

var_t b_lit_to_t_lit(const Circuit* circuit, var_t b_lit) {
    return var_id_to_t_lit(circuit, b_lit_to_var_id(circuit, b_lit));
}

static bool node_is_relevant(CircuitAbstraction* abs, var_t node_var) {
    if (abs->scope->node == 0) {
        // every node is relevant for quantifier prefix
        return true;
    }
    const node_shared* node = abs->scope->circuit->nodes[node_var];
    if (bit_vector_contains(node->relevant_for, abs->scope->scope_id)) {
        return true;
    }
    Scope* scope = abs->scope->prev;
    while (scope != NULL) {
        if (bit_vector_max(node->relevant_for) == scope->scope_id) {
            return true;
        }
        scope = scope->prev;
    }
    return false;
}

/**
 * Projects the maximal scope in node to current scope
 */
static var_t current_max(const node_shared* node, const Scope* scope) {
    const var_t max = bit_vector_max(node->influences);
    if (scope->max_depth != 0 && scope->max_depth < max) {
        return scope->max_depth;
    } else {
        return max;
    }
}

static void analyze_gate(CircuitAbstraction* abs, Scope* scope, bool negate, Gate* gate) {
    const Circuit* circuit = scope->circuit;
    
    const lit_t t_lit = node_to_t_lit(circuit, &gate->shared);
    const lit_t b_lit = node_to_b_lit(circuit, &gate->shared);
    
    bool input_max_scope_current = false;
    bool input_max_scope_outer = false;
    
    bool variable_of_outer_scope = false;
    bool variable_of_current_scope = false;
    
    bool all_gates_max_scope_outer = true;
    bool all_gates_max_scope_current = true;
    
    
    for (size_t i = 0; i < gate->num_inputs; i++) {
        const var_t input = lit_to_var(gate->inputs[i]);
        const node_type input_type = circuit->types[input];
        const node_shared* input_node = circuit->nodes[input];
        
        if (!node_is_relevant(abs, input)) {
            continue;
        }
        
        if (current_max(input_node, scope) == scope->depth) {
            input_max_scope_current = true;
        } else if (current_max(input_node, scope) < scope->depth) {
            input_max_scope_outer = true;
        }
        
        if (input_type == NODE_VAR) {
            if (bit_vector_max(input_node->influences) < scope->depth) {
                variable_of_outer_scope = true;
            } else if (bit_vector_contains(input_node->influences, scope->depth)) {
                variable_of_current_scope = true;
            }
        } else if (input_type == NODE_GATE) {
            if (current_max(input_node, scope) >= scope->depth) {
                all_gates_max_scope_outer = false;
            }
            if (current_max(input_node, scope) > scope->depth) {
                all_gates_max_scope_current = false;
            }
        } else {
            assert(input_type == NODE_SCOPE);
            all_gates_max_scope_outer = false;
            all_gates_max_scope_current = false;
        }
    }
    
    if (negate) {
        return;
    }
    
    if (!abs->options->use_combined_abstraction) {
        const bool need_b_lit = variable_of_current_scope && scope->num_next > 0;
        const bool need_t_lit = variable_of_outer_scope;
        
        if (need_b_lit) {
            int_vector_add(abs->b_lits, b_lit);
        }
        if (need_t_lit) {
            int_vector_add(abs->t_lits, t_lit);
        }
    } else {
        // Abstraction combination
        const bool gate_max_scope_current = current_max(&gate->shared, scope) <= scope->depth;
        const bool variable_b_lit = variable_of_current_scope && !gate_max_scope_current;
        const bool combination_b_lit = input_max_scope_current && !gate_max_scope_current;
        
        const bool gate_max_scope_outer = current_max(&gate->shared, scope) < scope->depth;
        const bool variable_t_lit = variable_of_outer_scope && !gate_max_scope_outer;
        const bool combination_t_lit = input_max_scope_outer && !gate_max_scope_outer;
        
        if (variable_b_lit) {
            assert(bit_vector_max(gate->shared.influences) > scope->depth);
            assert(scope->num_next > 0);
            int_vector_add(abs->b_lits, b_lit);
        }
        if (combination_b_lit) {
            if (!all_gates_max_scope_current) {
                for (size_t i = 0; i < gate->num_inputs; i++) {
                    const var_t input = lit_to_var(gate->inputs[i]);
                    const node_type input_type = circuit->types[input];
                    const node_shared* input_node = circuit->nodes[input];
                    
                    if (input_type != NODE_GATE) {
                        continue;
                    }
                    if (current_max(input_node, scope) != scope->depth) {
                        continue;
                    }
                    
                    var_t other_b_lit = node_to_b_lit(circuit, input_node);
                    int_vector_add_sorted(abs->b_lits, other_b_lit);
                }
            } else if (!variable_b_lit) {
                assert(bit_vector_max(gate->shared.influences) > scope->depth);
                assert(scope->num_next > 0);
                int_vector_add(abs->b_lits, b_lit);
            }
        }
        
        if (variable_t_lit) {
            int_vector_add(abs->t_lits, t_lit);
        }
        if (combination_t_lit) {
            if (!all_gates_max_scope_outer) {
                for (size_t i = 0; i < gate->num_inputs; i++) {
                    const var_t input = lit_to_var(gate->inputs[i]);
                    const node_type input_type = circuit->types[input];
                    const node_shared* input_node = circuit->nodes[input];
                    
                    if (input_type != NODE_GATE) {
                        continue;
                    }
                    if (current_max(input_node, scope) >= scope->depth) {
                        continue;
                    }
                    
                    var_t other_t_lit = node_to_t_lit(circuit, input_node);
                    int_vector_add_sorted(abs->t_lits, other_t_lit);
                }
            } else if (!variable_t_lit) {
                int_vector_add(abs->t_lits, t_lit);
            }
        }
    }
}

/**
 * Implements the constraint when a b-literal for given OR-gate may be set to true.
 */
static void encode_or_gate(CircuitAbstraction* abs, Scope* scope, bool negate, Gate* gate) {
    Circuit* circuit = scope->circuit;
    SATSolver* sat = negate ? abs->negation : abs->sat;
    
    const quantifier_type qtype = negate ? circuit_negate_quantifier_type(scope->qtype) : scope->qtype;
    
    const lit_t t_lit = node_to_t_lit(circuit, &gate->shared);
    const lit_t b_lit = node_to_b_lit(circuit, &gate->shared);
    
    if (gate->keep) {
        return;
    }
    
    for (size_t i = 0; i < gate->num_inputs; i++) {
        lit_t gate_input = gate->inputs[i];
        var_t input_var = lit_to_var(gate_input);
        
        if (!node_is_relevant(abs, input_var)) {
            continue;
        }
        
        // variable inputs have to be negated for universal quantifier
        lit_t transformed_input = (circuit->types[input_var] != NODE_VAR || qtype == QUANT_EXISTS) ? gate_input : -gate_input;
        
        const node_shared* occ_node = circuit->nodes[input_var];
        const node_type type = circuit->types[input_var];
        
        if (bit_vector_min(occ_node->influences) > scope->depth) {
            // FIXME: probably not needed since it implies
            assert(bit_vector_max(occ_node->influences) > scope->depth);
            continue;
        }
        
        
        if (type == NODE_VAR) {
            // input to the gate is a variable...
            
            if (bit_vector_contains(occ_node->influences, scope->depth)) {
                // ...of current scope
                satsolver_add(sat, transformed_input);
#ifdef CERTIFICATION
                if (abs->options->certify) {
                    certification_add_literal(abs->cert, transformed_input);
                }
#endif
            }
        } else if (type == NODE_SCOPE) {
            satsolver_add(sat, -node_to_b_lit(circuit, occ_node));
        } else {
            assert(type == NODE_GATE);
            
            const var_t other_b_lit = node_to_b_lit(circuit, occ_node);
            const var_t other_t_lit = node_to_t_lit(circuit, occ_node);
            
            if (current_max(occ_node, scope) < scope->depth) {
                if (!abs->options->use_combined_abstraction) {
                    satsolver_add(sat, other_b_lit);
#ifdef CERTIFICATION
                    if (abs->options->certify) {
                        assert(!int_vector_contains_sorted(abs->b_lits, other_b_lit));
                        certification_add_b_literal(abs->cert, abs, qtype, occ_node->id);
                    }
#endif
                } else if (int_vector_contains_sorted(abs->t_lits, other_t_lit)) {
                    satsolver_add(sat, other_t_lit);
#ifdef CERTIFICATION
                    if (abs->options->certify) {
                        certification_add_t_literal(abs->cert, abs, qtype, occ_node->id, other_b_lit);
                    }
#endif
                }

                continue;
            }
            
            assert(current_max(occ_node, scope) >= scope->depth);
            
            Gate* other_gate = (Gate*)occ_node;
            const gate_type other_type = normalize_gate_type(other_gate->type, qtype);
            if (other_type == GATE_OR) {
                satsolver_add(sat, node_to_b_lit(circuit, occ_node));
#ifdef CERTIFICATION
                if (abs->options->certify) {
                    certification_add_b_literal(abs->cert, abs, qtype, occ_node->id);
                }
#endif
            }
            if (current_max(occ_node, scope) == scope->depth) {
                // Implement 'chaining'
                satsolver_add(sat, node_to_b_lit(circuit, occ_node));
#ifdef CERTIFICATION
                if (abs->options->certify) {
                    certification_add_b_literal(abs->cert, abs, qtype, occ_node->id);
                }
#endif
            }
        }
    }
    
    const bool need_t_lit = int_vector_contains_sorted(abs->t_lits, t_lit);
    
    if (need_t_lit) {
        satsolver_add(sat, t_lit);
#ifdef CERTIFICATION
        if (abs->options->certify) {
            certification_add_t_literal(abs->cert, abs, qtype, gate->shared.id, b_lit);
        }
#endif
    }
    
    satsolver_add(sat, -b_lit);
    
    satsolver_add(sat, 0);
    
#ifdef CERTIFICATION
    if (abs->options->certify) {
        certification_define_b_literal(abs->cert, abs, qtype, gate->shared.id);
    }
#endif
}

static void append_or_gate(CircuitAbstraction* abs, Scope* scope, bool negate, Gate* gate, bool relevant_for_certification) {
    Circuit* circuit = scope->circuit;
    SATSolver* sat = negate ? abs->negation : abs->sat;
    const quantifier_type qtype = negate ? circuit_negate_quantifier_type(scope->qtype) : scope->qtype;
    
    const lit_t t_lit = node_to_t_lit(circuit, &gate->shared);
    const lit_t b_lit = node_to_b_lit(circuit, &gate->shared);
    
    assert(!gate->keep);
    
    if (bit_vector_min(gate->shared.influences) > scope->depth) {
        satsolver_add(sat, -b_lit);
        return;
    }
    
    for (size_t i = 0; i < gate->num_inputs; i++) {
        lit_t gate_input = gate->inputs[i];
        var_t var = lit_to_var(gate_input);
        
        if (!node_is_relevant(abs, var)) {
            continue;
        }
        
        // variable inputs have to be negated for universal quantifier
        lit_t transformed_input = (circuit->types[var] != NODE_VAR || qtype == QUANT_EXISTS) ? gate_input : -gate_input;
        node_shared* occ_node = circuit->nodes[var];
        const node_type type = circuit->types[var];
        
        if (bit_vector_min(occ_node->influences) > scope->depth) {
            satsolver_add(sat, -b_lit);
            continue;
        }
        
        if (type == NODE_VAR) {
            // Input to the gate is a variable...
            if (bit_vector_contains(occ_node->influences, scope->depth)) {
                // ...of current scope
                satsolver_add(sat, transformed_input);
            }
        } else if (type == NODE_SCOPE) {
            assert(transformed_input > 0);
            satsolver_add(sat, -var_id_to_b_lit(circuit, transformed_input));
        } else if (type == NODE_GATE) {
            
            const var_t other_b_lit = node_to_b_lit(circuit, occ_node);
            const var_t other_t_lit = node_to_t_lit(circuit, occ_node);
            
            if (current_max(occ_node, scope) < scope->depth) {
                if (!abs->options->use_combined_abstraction) {
                    satsolver_add(sat, other_b_lit);
                } else if (int_vector_contains_sorted(abs->t_lits, other_t_lit)) {
                    satsolver_add(sat, other_t_lit);
                }
                
                continue;
            }
            
            if (current_max(occ_node, scope) == scope->depth) {
                satsolver_add(sat, other_b_lit);
                continue;
            }
            
            assert(current_max(occ_node, scope) >= scope->depth);
            
            Gate* other_gate = (Gate*)occ_node;
            const gate_type other_type = normalize_gate_type(other_gate->type, qtype);
            if (other_type == GATE_OR) {
                append_or_gate(abs, scope, negate, other_gate, relevant_for_certification);
            } else {
                assert(other_type == GATE_AND);
                satsolver_add(sat, other_b_lit);
            }
        }
    }
    
    const bool need_t_lit = int_vector_contains_sorted(abs->t_lits, t_lit);
    
    if (need_t_lit) {
        satsolver_add(sat, t_lit);
    }
    
    const bool need_b_lit = int_vector_contains_sorted(abs->b_lits, b_lit);
    
    if (need_b_lit) {
        assert(scope->num_next > 0);
        // OR gate must be satisfied by this scope or an outer one when b-lit is true
        if (!negate) {
            assert(int_vector_contains_sorted(abs->b_lits, b_lit));
        }
        satsolver_add(sat, -b_lit);
    }
    
    // the clause is not closed intentionally
}

static void encode_and_gate(CircuitAbstraction* abs, Scope* scope, bool negate, Gate* gate) {
    Circuit* circuit = scope->circuit;
    SATSolver* sat = negate ? abs->negation : abs->sat;
    
    const quantifier_type qtype = negate ? circuit_negate_quantifier_type(scope->qtype) : scope->qtype;
    
    const lit_t t_lit = node_to_t_lit(circuit, &gate->shared);
    const lit_t b_lit = node_to_b_lit(circuit, &gate->shared);
    
    for (size_t i = 0; i < gate->num_inputs; i++) {
        lit_t gate_input = gate->inputs[i];
        var_t var = lit_to_var(gate_input);
        
        if (!node_is_relevant(abs, var)) {
            continue;
        }
        
        // variable inputs have to be negated for universal quantifier
        lit_t transformed_input = (circuit->types[var] != NODE_VAR || qtype == QUANT_EXISTS) ? gate_input : -gate_input;
        
        node_shared* occ_node = circuit->nodes[var];
        const node_type type = circuit->types[var];
        
        if (bit_vector_min(occ_node->influences) > scope->depth) {
            // FIXME: probably not needed since it implies
            assert(bit_vector_max(occ_node->influences) > scope->depth);
            continue;
        }
        
        if (type == NODE_VAR) {
            // Input to the gate is a variable...
            
            if (bit_vector_contains(occ_node->influences, scope->depth)) {
                // ...of current scope
                satsolver_add(sat, transformed_input);
                satsolver_add(sat, -b_lit);
                satsolver_add(sat, 0);
#ifdef CERTIFICATION
                if (abs->options->certify) {
                    certification_add_literal(abs->cert, transformed_input);
                }
#endif
            }
        } else if (type == NODE_SCOPE) {
            assert(transformed_input > 0);
            satsolver_add(sat, -var_id_to_b_lit(circuit, transformed_input));
            satsolver_add(sat, -b_lit);
            satsolver_add(sat, 0);
        } else {
            assert(type == NODE_GATE);
            
            const var_t other_b_lit = node_to_b_lit(circuit, occ_node);
            const var_t other_t_lit = node_to_t_lit(circuit, occ_node);
            
            if (current_max(occ_node, scope) < scope->depth) {
                if (!abs->options->use_combined_abstraction) {
                    satsolver_add(sat, other_b_lit);
                    satsolver_add(sat, -b_lit);
                    satsolver_add(sat, 0);
#ifdef CERTIFICATION
                    if (abs->options->certify) {
                        assert(!int_vector_contains_sorted(abs->b_lits, other_b_lit));
                        certification_add_b_literal(abs->cert, abs, qtype, occ_node->id);
                    }
#endif
                } else if (int_vector_contains_sorted(abs->t_lits, other_t_lit)) {
                    satsolver_add(sat, other_t_lit);
                    satsolver_add(sat, -b_lit);
                    satsolver_add(sat, 0);
#ifdef CERTIFICATION
                    if (abs->options->certify) {
                        certification_add_t_literal(abs->cert, abs, qtype, occ_node->id, other_b_lit);
                    }
#endif
                }
                
                continue;
            }
            
            
            Gate* other_gate = (Gate*)occ_node;
            const gate_type other_type = normalize_gate_type(other_gate->type, qtype);
            if (other_type == GATE_AND) {
                satsolver_add(sat, other_b_lit);
                satsolver_add(sat, -b_lit);
                satsolver_add(sat, 0);
#ifdef CERTIFICATION
                if (abs->options->certify) {
                    certification_add_b_literal(abs->cert, abs, qtype, other_gate->shared.id);
                }
#endif
            } else {
                assert(other_type == GATE_OR);
                if (current_max(occ_node, scope) == scope->depth) {
                    satsolver_add(sat, other_b_lit);
#ifdef CERTIFICATION
                    if (abs->options->certify) {
                        certification_add_b_literal(abs->cert, abs, qtype, other_gate->shared.id);
                    }
#endif
                } else {
                    append_or_gate(abs, scope, negate, other_gate, false);
                }
                satsolver_add(sat, -b_lit);
                satsolver_add(sat, 0);
            }
        }
    }
    
    const bool need_t_lit = int_vector_contains_sorted(abs->t_lits, t_lit);
    
    if (need_t_lit) {
        satsolver_add(sat, t_lit);
        satsolver_add(sat, -b_lit);
        satsolver_add(sat, 0);
#ifdef CERTIFICATION
        if (abs->options->certify) {
            certification_add_t_literal(abs->cert, abs, qtype, gate->shared.id, b_lit);
        }
#endif
    }
    
#ifdef CERTIFICATION
    if (abs->options->certify) {
        certification_define_b_literal(abs->cert, abs, qtype, gate->shared.id);
    }
#endif
}

static void encode_gate(CircuitAbstraction* abs, Scope* scope, bool negate, var_t id) {
    Circuit* circuit = scope->circuit;
    Gate* gate = circuit->nodes[id];
    const quantifier_type qtype = negate ? circuit_negate_quantifier_type(scope->qtype) : scope->qtype;
    const gate_type type = normalize_gate_type(gate->type, qtype);

    if (bit_vector_min(gate->shared.influences) > scope->depth) {
        return;
    }
    if (abs->options->use_combined_abstraction && current_max(&gate->shared, scope) < scope->depth) {
        return;
    }

    if (type == GATE_AND) {
        encode_and_gate(abs, scope, negate, gate);
    } else {
        assert(type == GATE_OR);
        encode_or_gate(abs, scope, negate, gate);
    }
}

static void encode_node_scope(CircuitAbstraction* abs, Scope* scope, bool negate, var_t id) {
    Circuit* circuit = scope->circuit;
    SATSolver* sat = negate ? abs->negation : abs->sat;
    const quantifier_type qtype = negate ? circuit_negate_quantifier_type(scope->qtype) : scope->qtype;
    
    ScopeNode* scope_node = circuit->nodes[id];
    
    var_t t_lit = node_to_t_lit(circuit, &scope_node->shared);
    const var_t b_lit = node_to_b_lit(circuit, &scope_node->shared);
    
    if (scope_node->scope == scope) {
        // add t-lit
        /*satsolver_add(sat, b_lit);
        satsolver_add(sat, t_lit);
        satsolver_add(sat, 0);*/
        if (!negate) {
            int_vector_add_sorted(abs->t_lits, t_lit);
        }
    } else if (scope_node->scope->prev == scope) {
        if (!negate) {
            int_vector_add_sorted(abs->b_lits, b_lit);
        }
    }
    
    const var_t sub_var = lit_to_var(scope_node->sub);
    const node_type sub_type = circuit->types[sub_var];
    assert(sub_type != NODE_VAR);
    
    // need to check the type of sub_var
    
    if (sub_type == NODE_SCOPE) {
        satsolver_add(sat, b_lit);
        satsolver_add(sat, -var_id_to_b_lit(circuit, sub_var));
        satsolver_add(sat, 0);
    } else {
        assert(sub_type == NODE_GATE);
        
        Gate* other_gate = circuit->nodes[sub_var];
        const gate_type other_type = normalize_gate_type(other_gate->type, qtype);
        if (other_type == GATE_AND) {
            var_t other_b_lit = node_to_b_lit(circuit, &other_gate->shared);
            satsolver_add(sat, create_lit(other_b_lit, false));
            satsolver_add(sat, create_lit(b_lit, false));
            satsolver_add(sat, 0);
        } else {
            assert(other_type == GATE_OR);
            append_or_gate(abs, scope, negate, other_gate, false);
            satsolver_add(sat, create_lit(b_lit, false));
            satsolver_add(sat, 0);
        }
    }
}

static void fix_output_value(CircuitAbstraction* abs, Scope* scope, bool negate) {
    Circuit* circuit = scope->circuit;
    SATSolver* sat = negate ? abs->negation : abs->sat;
    
    assert(circuit->output > 0);
    assert(circuit->types[circuit->output] == NODE_GATE);
    Gate* gate = circuit->nodes[circuit->output];
    const quantifier_type qtype = negate ? circuit_negate_quantifier_type(scope->qtype) : scope->qtype;
    const gate_type type = normalize_gate_type(gate->type, qtype);
    
    if (type == GATE_AND) {
        satsolver_add(sat, var_id_to_b_lit(circuit, circuit->output));
    } else {
        assert(!abs->options->certify || certification_queue_is_empty(abs->cert));
        append_or_gate(abs, scope, negate, gate, false);
        assert(!abs->options->certify || certification_queue_is_empty(abs->cert));
    }
    satsolver_add(sat, 0);
}

static void circuit_abstraction_build_sat_instance(CircuitAbstraction* abs, Scope* scope, bool negate) {
    Circuit* circuit = scope->circuit;
    SATSolver* sat = negate ? abs->negation : abs->sat;
    
    // Assumption on the instance (guaranteed if preprocessing is enabled)
    // No empty scopes (only exception is the first scope if there are no variables at all)
    assert(vector_count(scope->vars) == 0 && scope->scope_id == 1 || vector_count(scope->vars) > 0);
    
    if (!negate) {
        for (var_t i = 1; i <= circuit->max_num; i++) {
            assert(circuit->nodes[i] != NULL);
            node_type type = circuit->types[i];
            
            if (!node_is_relevant(abs, i)) {
                continue;
            }
            if (type != NODE_GATE) {
                continue;
            }
            Gate* gate = circuit->nodes[i];
            analyze_gate(abs, scope, negate, gate);
        }
    }
    
    for (var_t i = 1; i <= circuit->max_num; i++) {
        assert(circuit->nodes[i] != NULL);
        node_type type = circuit->types[i];
        
        if (!node_is_relevant(abs, i)) {
            continue;
        }
        
        if (type == NODE_SCOPE) {
            encode_node_scope(abs, scope, negate, i);
        } else if (type == NODE_GATE) {
            encode_gate(abs, scope, negate, i);
        } else {
            assert(type == NODE_VAR);
        }
    }
    
    fix_output_value(abs, scope, negate);
    
    if (logging_get_verbosity() >= VERBOSITY_ALL) {
        satsolver_print(sat);
        printf("\n");
    }
}


CircuitAbstraction* circuit_abstraction_init(SolverOptions* options, certification* cert, Scope* scope, CircuitAbstraction* prev) {
    CircuitAbstraction* abs = malloc(sizeof(CircuitAbstraction));
    abs->options = options;
    
    abs->cert = cert;
    
    abs->scope = scope;
    abs->prev = prev;
    abs->next = calloc(scope->num_next, sizeof(CircuitAbstraction*));
    
    abs->sat = satsolver_init();
    abs->negation = satsolver_init();
    
    abs->t_lits = int_vector_init();
    abs->b_lits = int_vector_init();
    abs->assumptions = int_vector_init();
    abs->entry = bit_vector_init(scope->circuit->max_num, var_id_to_t_lit(scope->circuit, scope->circuit->max_num) + 1);
    abs->local_unsat_core = int_vector_init();
    abs->sat_solver_assumptions = int_vector_init();
    
    abs->statistics = statistics_init(10000);
    
#ifdef PARALLEL_SOLVING
    pthread_mutex_init(&abs->mutex, NULL);
    semaphore_init(&abs->sub_finished, 0);
    abs->num_started = 0;
#endif
    
    // create variables needed
    for (size_t i = 0; i < 2 * scope->circuit->max_num; i++) {
        satsolver_new_variable(abs->sat);
        satsolver_new_variable(abs->negation);
    }
    
    if (abs->scope->num_next > 0 && abs->options->use_partial_deref) {
        fixme("save original clauses\n");
        //satsolver_save_original_clauses(abs->sat);
    }
    //satsolver_save_original_clauses(abs->negation);
    
    //satsolver_adjust(abs->sat, 2 * abs->scope->circuit->max_num);
    //satsolver_adjust(abs->negation, 2 * abs->scope->circuit->max_num);
    
    logging_debug("Level %d\n", scope->scope_id);
    circuit_abstraction_build_sat_instance(abs, scope, false);
    circuit_abstraction_build_sat_instance(abs, scope, true);
    
    if (logging_get_verbosity() >= VERBOSITY_ALL) {
        for (size_t i = 0; i < int_vector_count(abs->t_lits); i++) {
            int t_lit = int_vector_get(abs->t_lits, i);
            logging_debug("t%d ", t_lit_to_var_id(abs->scope->circuit, t_lit));
        }
        logging_debug("\n");
        
        for (size_t i = 0; i < int_vector_count(abs->b_lits); i++) {
            int b_lit = int_vector_get(abs->b_lits, i);
            logging_debug("b%d ", b_lit_to_var_id(abs->scope->circuit, b_lit));
        }
        logging_debug("\n\n\n");
    }
    
    assert(int_vector_is_sorted(abs->t_lits));
    assert(int_vector_is_sorted(abs->b_lits));
    
    return abs;
}

void circuit_abstraction_free_recursive(CircuitAbstraction* abstraction) {
    for (size_t i = 0; i < abstraction->scope->num_next; i++) {
        circuit_abstraction_free_recursive(abstraction->next[i]);
    }
    circuit_abstraction_free(abstraction);
}

void circuit_abstraction_free(CircuitAbstraction* abstraction) {
    satsolver_free(abstraction->sat);
    satsolver_free(abstraction->negation);
    
    statistics_free(abstraction->statistics);
    
    int_vector_free(abstraction->t_lits);
    int_vector_free(abstraction->b_lits);
    int_vector_free(abstraction->assumptions);
    bit_vector_free(abstraction->entry);
    int_vector_free(abstraction->local_unsat_core);
    
    free(abstraction->next);
    free(abstraction);
}

void circuit_abstraction_get_assumptions(CircuitAbstraction* abstraction) {
    Circuit* circuit = abstraction->scope->circuit;
    
    int_vector_reset(abstraction->assumptions);
    for (size_t i = 0; i < abstraction->scope->num_next; i++) {
        CircuitAbstraction* next = abstraction->next[i];
        assert(next != NULL);
        bit_vector_reset(next->entry);
    }
    
    logging_debug("Disabled: ");
    for (size_t i = 0; i < int_vector_count(abstraction->b_lits); i++) {
        const lit_t b_lit = int_vector_get(abstraction->b_lits, i);
        int value;
        if (abstraction->options->use_partial_deref) {
            fixme("partial deref\n");
            //value = satsolver_deref_partial(abstraction->sat, b_lit);
            value = satsolver_value(abstraction->sat, b_lit);
        } else {
            value = satsolver_value(abstraction->sat, b_lit);
        }
        if (value >= 0) {
            continue;
        }
        // b-lit was disabled by SAT solver
        
        if (abstraction->options->assignment_b_lit_minimization) {
            const var_t node_id = b_lit_to_var_id(circuit, b_lit);
            const node_type type = circuit->types[node_id];
            assert(type != NODE_VAR);
            const int circuit_value = circuit_get_value(circuit, node_id);
            const int normalized_value = abstraction->scope->qtype == QUANT_FORALL ? -circuit_value : circuit_value;
            
            if (type == NODE_GATE) {
                Gate* gate = circuit->nodes[node_id];
                const gate_type gate_type = abstraction->scope->qtype == QUANT_EXISTS ? gate->type : circuit_negate_gate_type(gate->type);
                if (circuit_value == 0 && gate_type == GATE_AND) {
                    continue;
                }
                if (normalized_value > 0) {
                    // node is already satisified
                    continue;
                }
            }
        }
        
        logging_debug("b%d ", b_lit_to_var_id(circuit, b_lit));
        int_vector_add(abstraction->assumptions, b_lit);
        
        const lit_t t_lit = b_lit_to_t_lit(circuit, b_lit);
        size_t num_added = 0;
        for (size_t j = 0; j < abstraction->scope->num_next; j++) {
            CircuitAbstraction* next = abstraction->next[j];
            assert(next != NULL);
            if (int_vector_contains_sorted(next->t_lits, t_lit)) {
                bit_vector_add(next->entry, t_lit);
                num_added++;
            }
        }
        assert(num_added > 0);
    }
    logging_debug("\n");
    
    for (size_t i = 0; i < int_vector_count(abstraction->t_lits); i++) {
        const lit_t t_lit = int_vector_get(abstraction->t_lits, i);
        if (bit_vector_contains(abstraction->entry, t_lit)) {
            continue;
        }
        const lit_t b_lit = t_lit_to_b_lit(circuit, t_lit);
        if (!int_vector_contains_sorted(abstraction->b_lits, b_lit)) {
            size_t num_added = 0;
            for (size_t j = 0; j < abstraction->scope->num_next; j++) {
                CircuitAbstraction* next = abstraction->next[j];
                assert(next != NULL);
                if (int_vector_contains_sorted(next->t_lits, t_lit)) {
                    bit_vector_add(next->entry, t_lit);
                    num_added++;
                }
            }
            assert(num_added >= 0);
        }
    }
}

void circuit_abstraction_assume_t_literals(CircuitAbstraction* abstraction, bool negation) {
    int_vector_reset(abstraction->sat_solver_assumptions);
    SATSolver* sat = negation ? abstraction->negation : abstraction->sat;
    for (size_t i = 0; i < int_vector_count(abstraction->t_lits); i++) {
        lit_t t_lit = int_vector_get(abstraction->t_lits, i);
        const var_t var_id = t_lit_to_var_id(abstraction->scope->circuit, t_lit);
        if (!bit_vector_contains(abstraction->entry, t_lit)) {
            t_lit = -t_lit;
        }
        if (negation) {
            t_lit = -t_lit;
        }
        satsolver_assume(sat, t_lit);
        logging_debug("t%d ", create_lit_from_value(var_id, t_lit));
        if (t_lit < 0) {
            int_vector_add(abstraction->sat_solver_assumptions, t_lit);
        }
    }
    logging_debug("\n");
}


static void assume_current_assignment(CircuitAbstraction* abstraction) {
    for (size_t i = 0; i < vector_count(abstraction->scope->vars); i++) {
        const Var* var = vector_get(abstraction->scope->vars, i);
        const lit_t sat_var = create_lit(var->shared.id, false);
        const int value = satsolver_value(abstraction->sat, sat_var);
        const lit_t sat_lit = create_lit_from_value(sat_var, value);
        if (value != 0) {
            logging_debug("%d ", sat_lit);
            satsolver_assume(abstraction->negation, sat_lit);
        }
    }
    logging_debug("\n");
}

void circuit_abstraction_dual_propagation(CircuitAbstraction* abstraction) {
    Circuit* circuit = abstraction->scope->circuit;
    
    assume_current_assignment(abstraction);
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
        logging_fatal("An internal solver error ocurred due to a bad abstraction entry.\nPlease consider sending a bug report to tentrup@react.uni-saarland.de\n");
    }
    
    // Reset variable assignments
    /*for (size_t i = 0; i < vector_count(abstraction->scope->vars); i++) {
        Var* var = vector_get(abstraction->scope->vars, i);
        var->shared.value = 0;
    }*/
    
    bit_vector_reset(abstraction->entry);
    logging_debug("min ");
    for (size_t i = 0; i < int_vector_count(abstraction->sat_solver_assumptions); i++) {
        lit_t failed_t_lit = int_vector_get(abstraction->sat_solver_assumptions, i);
        assert(failed_t_lit < 0);
        if (!satsolver_failed(abstraction->negation, failed_t_lit)) {
            continue;
        }
        failed_t_lit = -failed_t_lit;
        assert(failed_t_lit > 0);
        
        const var_t node = t_lit_to_var_id(circuit, failed_t_lit);
        logging_debug("t%d ", node);
        
        bit_vector_add(abstraction->entry, failed_t_lit);
        
#ifdef CERTIFICATION
        if (abstraction->options->certify) {
            certification_add_t_literal(abstraction->cert, abstraction, abstraction->scope->qtype, node, var_id_to_b_lit(circuit, node));
        }
#endif
    }
    logging_debug("\n");
    
#ifdef CERTIFICATION
    if (abstraction->options->certify) {
        certification_append_function_case(abstraction->cert, abstraction);
    }
#endif
}

void circuit_abstraction_get_unsat_core(CircuitAbstraction* abstraction) {
    Circuit* circuit = abstraction->scope->circuit;
    bit_vector_reset(abstraction->entry);
    logging_debug("unsat: ");
    for (size_t i = 0; i < int_vector_count(abstraction->sat_solver_assumptions); i++) {
        lit_t failed_t_lit = int_vector_get(abstraction->sat_solver_assumptions, i);
        assert(failed_t_lit < 0);
        if (!satsolver_failed(abstraction->sat, failed_t_lit)) {
            continue;
        }
        failed_t_lit = -failed_t_lit;
        assert(failed_t_lit > 0);
        
        const lit_t node_id = t_lit_to_var_id(circuit, failed_t_lit);
        bit_vector_add(abstraction->entry, failed_t_lit);
        logging_debug("-t%d ", node_id);
    }
    logging_debug("\n");
}

void circuit_abstraction_adjust_local_unsat_core(CircuitAbstraction* abstraction, CircuitAbstraction* child) {
    for (var_t t_lit = bit_vector_init_iteration(child->entry); bit_vector_iterate(child->entry); t_lit = bit_vector_next(child->entry)) {
        assert(t_lit > 0);
        //assert(!int_vector_contains_sorted(abstraction->local_unsat_core, t_lit));
        int_vector_add(abstraction->local_unsat_core, t_lit);
        fixme("adjust default phase\n");
        //satsolver_set_default_phase_lit(abstraction->sat, t_lit_to_b_lit(abstraction->scope->circuit, t_lit), 1);
    }
}
