//
//  circuit_check.c
//  caqe-qcir
//
//  Created by Leander Tentrup on 04.02.16.
//  Copyright © 2016 Saarland University. All rights reserved.
//

#include "circuit_check.h"
#include "logging.h"

static bool circuit_check_nodes(Circuit* circuit) {
    for (size_t i = 1; i <= circuit->max_num; i++) {
        if (circuit->types[i] == NODE_GATE) {
            Gate* gate = circuit->nodes[i];
            
            assert(gate->num_inputs <= gate->size_inputs);
            
            size_t num_inputs = 0;
            for (size_t j = 0; j < gate->num_inputs; j++) {
                lit_t gate_input = gate->inputs[j];
                
                if (circuit->phase == ENCODED) {
                    // If circuit is encoded, the id of child is strictly smaller than parent
                    assert(lit_to_var(gate_input) < gate->shared.id);
                }
                num_inputs++;
            }
            assert(num_inputs == gate->num_inputs);
            
            if (circuit->phase == ENCODED && lit_to_var(circuit->output) == gate->shared.id) {
                // Output has no occurrences
                assert(gate->shared.num_occ == 0);
            }
            
            // only allow empty gates if they are the output
            //assert(gate->size != 0 || gate->shared.id == circuit->output);
        } else if (circuit->types[i] == NODE_SCOPE) {
            ScopeNode* node = circuit->nodes[i];
            assert(node->sub != 0);
            if (circuit->phase == ENCODED) {
                // If circuit is encoded, the id of child is strictly smaller than parent
                assert(lit_to_var(node->sub) < node->shared.id);
            }
        }
    }
    return true;
}

/**
 * Check if node->num_occ is correct
 */
static bool check_occurrences(Circuit* circuit) {
    size_t* num_occurrences = calloc(circuit->max_num + 1, sizeof(size_t));
    
    for (size_t i = 1; i <= circuit->max_num; i++) {
        if (circuit->nodes[i] == NULL) {
            continue;
        }
        
        const node_type type = circuit->types[i];
        if (type == NODE_GATE) {
            Gate* gate = circuit->nodes[i];
            for (size_t j = 0; j < gate->num_inputs; j++) {
                var_t var = lit_to_var(gate->inputs[j]);
                num_occurrences[var]++;
            }
        } else if (type == NODE_SCOPE) {
            ScopeNode* scope_node = circuit->nodes[i];
            num_occurrences[lit_to_var(scope_node->sub)]++;
        }
    }
    
    for (size_t i = 1; i <= circuit->max_num; i++) {
        size_t num_occ = num_occurrences[i];
        if (circuit->nodes[i] == NULL) {
            //assert(num_occ == 0);
            continue;
        }
        
        node_shared* node = circuit->nodes[i];
        assert(node->num_occ == num_occ);
    }
    free(num_occurrences);
    return true;
}

static bool check_values(Circuit* circuit) {
    for (size_t i = 1; i <= circuit->max_num; i++) {
        if (circuit->nodes[i] == NULL) {
            continue;
        }
        node_shared* node = circuit->nodes[i];
        assert(node->id == lit_to_var(circuit->output) || node->value == 0);
        if (circuit->types[i] == NODE_GATE) {
            //Gate* gate = circuit->nodes[i];
            //assert(!gate->conflict);
        }
    }
    return true;
}

static bool check_scope_influence(Circuit* circuit) {
    assert(circuit);
    return true;
    /*for (size_t i = 1; i <= circuit->max_num; i++) {
        if (circuit->nodes[i] == NULL) {
            continue;
        }
        node_shared* node = circuit->nodes[i];
        if (circuit->types[i] == NODE_VAR) {
            Var* var = circuit->nodes[i];
            if (var->removed) {
                continue;
            }
            assert(bit_vector_min(node->scopes) == bit_vector_max(node->scopes));
            assert(bit_vector_contains(node->scopes, var->scope->scope_id));
            continue;
        } else if (circuit->types[i] == NODE_GATE) {
            Gate* gate = circuit->nodes[i];
            bit_vector* result = bit_vector_init(0, circuit->last_scope_id);
            
            for (size_t j = 0; j < gate->num_inputs; j++) {
                lit_t gate_input = gate->inputs[j];
                var_t var = lit_to_var(gate_input);
                node_shared* occ_node = circuit->nodes[var];
                bit_vector_update_or(result, occ_node->scopes);
            }
            
            if (gate->num_inputs > 0) {
                assert(bit_vector_equal(result, gate->shared.scopes));
            } else {
                assert(bit_vector_min(node->scopes) == 1);
                assert(bit_vector_max(node->scopes) == 1);
            }
            bit_vector_free(result);
        } else {
            assert(circuit->types[i] == NODE_SCOPE);
            ScopeNode* scope_node = circuit->nodes[i];
            bit_vector* result = bit_vector_init(0, circuit->last_scope_id);
            var_t var = lit_to_var(scope_node->sub);
            node_shared* occ_node = circuit->nodes[var];
            bit_vector_update_or(result, occ_node->scopes);
            bit_vector_remove(result, scope_node->scope->scope_id);
            assert(bit_vector_equal(result, scope_node->shared.scopes));
            bit_vector_free(result);
        }
    }
    return true;*/
}

static bool check_scopes_recursively(Circuit* circuit, Scope* scope) {
    bool result = true;
    for (size_t i = 0; i < scope->num_next; i++) {
        assert(scope->next[i] != scope);
        result &= check_scopes_recursively(circuit, scope->next[i]);
        
        assert(scope->next[i]->prev == scope);
    }
    
    if (scope->node == 0) {
        // quantifier is in prenex
        assert(scope->depth == scope->scope_id);
    }
    
    if (scope == circuit->top_level) {
        assert(scope->prev == NULL);
    } else {
        assert(vector_count(scope->vars) > 0);
        if (scope->node != 0) {
            assert(circuit->types[scope->node] == NODE_SCOPE);
            ScopeNode* scope_node = circuit->nodes[scope->node];
            assert(scope_node->scope == scope);
        }
    }
    
    return result;
}

static bool check_variables(Circuit* circuit) {
    for (size_t i = 1; i <= circuit->max_num; i++) {
        if (circuit->types[i] != NODE_VAR) {
            continue;
        }
        Var* var = circuit->nodes[i];
        
        // Check that variable is contained in scope
        Scope* var_scope = var->scope;
        bool contained = false;
        for (size_t j = 0; j < vector_count(var_scope->vars); j++) {
            Var* other = vector_get(var_scope->vars, j);
            if (var == other) {
                contained = true;
            }
        }
        assert(contained);
    }
    return true;
}

static bool check_quantifiers_are_consecutive_recursive(Circuit* circuit, Scope* scope) {
    if (circuit->phase != ENCODED) {
        return true;
    }
    for (size_t i = 0; i < scope->num_next; i++) {
        Scope* child = scope->next[i];
        check_quantifiers_are_consecutive_recursive(circuit, child);
        
        assert(scope->qtype != child->qtype);
    }
    return true;
}

/**
 * Checks if data structures representing circuit are correct
 */
bool circuit_check(Circuit* circuit) {
    bool result = true;
    result &= circuit_check_all_nodes_defined(circuit);
    result &= circuit_check_nodes(circuit);
    result &= check_occurrences(circuit);
    result &= check_values(circuit);
    result &= check_scope_influence(circuit);
    result &= check_scopes_recursively(circuit, circuit->top_level);
    result &= check_variables(circuit);
    result &= check_quantifiers_are_consecutive_recursive(circuit, circuit->top_level);
    return result;
}

bool circuit_check_occurrences(Circuit* circuit) {
    return check_occurrences(circuit);
}

static bool is_defined(Circuit* circuit, lit_t literal) {
    var_t var = lit_to_var(literal);
    switch (circuit->types[var]) {
        case NODE_VAR:
        case NODE_GATE:
        case NODE_SCOPE:
            assert(circuit->nodes[var] != NULL);
            return true;
            
        default:
            return false;
    }
}

bool circuit_check_all_nodes_defined(Circuit* circuit) {
    bool all_defined = true;
    for (size_t i = 1; i <= circuit->max_num; i++) {
        if (circuit->types[i] == NODE_GATE) {
            Gate* gate = circuit->nodes[i];
            for (size_t j = 0; j < gate->num_inputs; j++) {
                lit_t gate_input = gate->inputs[j];
                if (!is_defined(circuit, gate_input)) {
                    logging_error("Node %d is not defined (occurred in node %d)\n", lit_to_var(gate_input), gate->shared.id);
                    all_defined = false;
                }
            }
        } else if (circuit->types[i] == NODE_SCOPE) {
            ScopeNode* node = circuit->nodes[i];
            if (!is_defined(circuit, node->sub)) {
                logging_error("Node %d is not defined (occurred in node %d)\n", lit_to_var(node->sub), node->shared.id);
                all_defined = false;
            }
        }
    }
    return all_defined;
}
