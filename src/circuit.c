//
//  circuit.c
//  caqe-qcir
//
//  Created by Leander Tentrup on 01.07.15.
//  Copyright (c) 2015 Saarland University. All rights reserved.
//

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "circuit.h"
#include "circuit_check.h"
#include "circuit_print.h"
#include "config.h"
#include "logging.h"
#include "util.h"
#include "map.h"

#define EVALUATION_NO_MAX (-1)


void remove_var(Circuit*, var_t);
void remove_gate(Circuit*, var_t);
void remove_scope(Circuit*, Scope*);

static void enlarge(void** buffer, size_t* size, size_t bytes, size_t num) {
    size_t old_size = *size;
    size_t new_size = old_size > 0 ? old_size * 2 : 1;
    
    void* old_buffer = *buffer;
    void* new_buffer = calloc(new_size, bytes);
    if (num > 0) {
        // copy old values
        memcpy(new_buffer, old_buffer, num * bytes);
    }
    free(old_buffer);
    
    *size = new_size;
    *buffer = new_buffer;
}

static void import_literal(Circuit* circuit, lit_t literal) {
    const var_t variable = lit_to_var(literal);
    if (variable > circuit->max_num) {
        circuit_adjust(circuit, variable);
    }
}


Circuit* circuit_init() {
    Circuit* circuit = malloc(sizeof(Circuit));
    circuit->types = NULL;
    circuit->nodes = NULL;
    circuit->max_num = 0;
    circuit->size = 0;
    circuit->output = -1;
    circuit->vars = vector_init();
    circuit->phase = BUILDING;
    circuit->num_vars = 0;
    
    // Scope handling
    circuit->current_scope_id = circuit->max_scope_id = 1;
    circuit->current_depth = circuit->max_depth = 1;
    // Top-level Scope is existential (for free variables)
    circuit->previous_scope = NULL;
    circuit->top_level = circuit_init_scope(circuit, QUANT_EXISTS);
    
    circuit->queue = NULL;
    
    return circuit;
}

/*static void free_scope_recursively(Scope* scope) {
    assert(scope);
    return;
    if (scope == NULL) {
        return;
    }
    //free_scope_recursively(scope->next);
    vector_free(scope->vars);
    free(scope);
}*/

void circuit_free(Circuit* circuit) {
    assert(circuit);
    return;
    
    /*for (size_t i = 1; i <= circuit->max_num; i++) {
        node_shared* node = circuit->nodes[i];
        if (node == NULL) {
            continue;
        }
        if (circuit->types[i] == NODE_GATE) {
            Gate* gate = circuit->nodes[i];
            Occ* next = NULL;
            for (Occ* occ = gate->first_occ; occ != NULL; occ = next) {
                next = occ->next_in_gate;
                free(occ);
            }
            free(gate);
        } else {
            assert(circuit->types[i] == NODE_VAR);
            Var* var = circuit->nodes[i];
            free(var);
        }
    }
    free(circuit->nodes);
    free(circuit->types);
    vector_free(circuit->vars);
    free_scope_recursively(circuit->top_level);
    free(circuit);*/
}

void circuit_adjust(Circuit* circuit, size_t max_num) {
    if (max_num < circuit->size) {
        circuit->max_num = max_num;
        return;
    }
    // use exponential growth strategy
    size_t new_size = (circuit->size == 0) ? 1 : 2 * circuit->size;
    while (new_size < max_num) {
        new_size *= 2;
    }
    assert(new_size > 0);
    node_type* types = calloc(new_size + 1, sizeof(node_type));
    node* nodes = calloc(new_size + 1, sizeof(node*));
    if (circuit->max_num > 0) {
        // enlarge buffer, copy old values
        memcpy(types, circuit->types, (circuit->max_num + 1) * sizeof(node_type));
        free(circuit->types);
        
        memcpy(nodes, circuit->nodes, (circuit->max_num + 1) * sizeof(node*));
        free(circuit->nodes);
    }
    circuit->max_num = max_num;
    circuit->size = new_size;
    circuit->types = types;
    circuit->nodes = nodes;
}

void circuit_set_output(Circuit* circuit, lit_t node_id) {
    api_expect(circuit->output == -1, "output can only be set once\n");
    circuit->output = node_id;
}

static void init_node_shared(node_shared* shared, var_t id) {
    shared->id = id;
    shared->orig_id = id;
    
    shared->num_occ = 0;
    
    shared->value = 0;
    
    shared->influences = NULL;
    shared->relevant_for = NULL;
    shared->dfs_processed = false;
}

Var* circuit_new_var(Circuit* circuit, Scope* scope, var_t var_id) {
    api_expect(var_id > 0, "variables must be greater than 0\n");
    Var* var = malloc(sizeof(Var));
    
    var->scope = scope;
    var->removed = false;
    var->polarity = POLARITY_UNDEFINED;
    var->var_id = circuit->num_vars++;
    var->orig_quant = scope->qtype;
    
    init_node_shared(&var->shared, var_id);
    
    import_literal(circuit, var_id);
    circuit->nodes[var_id] = (node*)var;
    circuit->types[var_id] = NODE_VAR;
    
    vector_add(scope->vars, var);
    vector_add(circuit->vars, var);
    
    return var;
}

// Scopes

static void link_scope(Scope* prev, Scope* next) {
    
    for (size_t i = 0; i < prev->num_next; i++) {
        if (prev->next[i] == next) {
            return;
        }
    }
    
    const size_t old_num_scopes = prev->num_next;
    prev->num_next++;
    Scope** new_next = malloc(prev->num_next * sizeof(Scope*));
    for (size_t i = 0; i < old_num_scopes; i++) {
        new_next[i] = prev->next[i];
    }
    new_next[prev->num_next - 1] = next;
    free(prev->next);
    prev->next = new_next;
    next->prev = prev;
}


static void unlink_scope(Scope* prev, Scope* current) {
    // Remove pointer to current from prev->scopes
    size_t j = 0;
    for (size_t i = 0; i < prev->num_next; i++) {
        if (prev->next[i] != current) {
            prev->next[j] = prev->next[i];
            j++;
        }
    }
    assert(j == prev->num_next - 1);
    prev->num_next = j;
    current->prev = NULL;
    
    if (prev->num_next == 0) {
        free(prev->next);
        prev->next = NULL;
    }
}

static void free_scope_node(Circuit* circuit, ScopeNode* scope_node) {
    //api_expect(circuit->phase == ENCODED || circuit->phase == PROPAGATION, "circuit must be encoded first\n");
    assert(scope_node != NULL);
    
    circuit->nodes[scope_node->shared.id] = NULL;
    circuit->types[scope_node->shared.id] = 0;
    free(scope_node);
}

static void remove_scope_node(Circuit* circuit, var_t node_id) {
    api_expect(circuit->phase == ENCODED || circuit->phase == PROPAGATION, "circuit must be encoded first\n");
    ScopeNode* scope_node = circuit->nodes[node_id];
    assert(scope_node != NULL);
    
    logging_debug("Remove scope node %d (%d), value %d\n", node_id, scope_node->shared.orig_id, scope_node->shared.value);
    
    Scope* scope = scope_node->scope;
    assert(vector_count(scope->vars) == 0 || scope_node->shared.value != 0);
    while (vector_count(scope->vars) > 0) {
        Var* var = vector_get(scope->vars, 0);
        remove_var(circuit, var->shared.id);
    }
    
    bool replace_with_singleton_gate = vector_count(scope->vars) == 0 && scope_node->shared.value == 0;
    node_shared shared = scope_node->shared;
    lit_t sub = scope_node->sub;
    
    free_scope_node(circuit, scope_node);
    
    if (replace_with_singleton_gate) {
        // When the scope corresponding to the node becomes empty, we replace
        // the node by a singleton gate, keeping the circuit encoding
        
        Gate* gate = circuit_add_gate(circuit, shared.id, GATE_AND);
        circuit_add_to_gate(circuit, gate, sub);
        gate->shared = shared;
        assert(shared.value == 0);
    } else {
        node_shared* sub_node = circuit->nodes[lit_to_var(sub)];
        assert(sub_node != NULL);
        assert(sub_node->num_occ > 0);
        sub_node->num_occ--;
    }
}

static void free_scope(Circuit* circuit, Scope* scope) {
    assert(scope->prev == NULL);
    assert(scope != circuit->top_level);
    
    assert(vector_count(scope->vars) == 0);
    
    if (scope->node != 0) {
        // Remove scope node from circuit
        /*ScopeNode* scope_node = circuit->nodes[scope->node];
         assert(scope_node != NULL);
         assert(scope_node->scope == scope);*/
        remove_scope_node(circuit, scope->node);
    }
    
    assert(scope->num_next == 0);
    free(scope->next);
    vector_free(scope->vars);
    free(scope);
}

/**
 * Merge scope `from` into scope `to`
 *
 * Assumes that there is an i such that to->next[i] == from
 */
static void merge_scopes(Circuit* circuit, Scope* from, Scope* to) {
    assert(from->qtype == to->qtype);
    
#ifndef NDEBUG
    bool found = false;
    for (size_t i = 0; i < to->num_next; i++) {
        if (to->next[i] == from) {
            found = true;
            break;
        }
    }
    assert(found);
#endif
    
    // Copy variables
    for (size_t i = 0; i < vector_count(from->vars); i++) {
        Var* var = vector_get(from->vars, i);
        vector_add(to->vars, var);
        var->scope = to;
    }
    vector_reset(from->vars);
    
    // Copy pointer from->next to to->next
    // note that by recursion, the scopes in from->next have a different quantifier type
    unlink_scope(to, from);
    while (from->num_next > 0) {
        Scope* from_next = from->next[0];
        assert(from_next->qtype == circuit_negate_quantifier_type(to->qtype));
        unlink_scope(from, from_next);
        link_scope(to, from_next);
    }
    
    if (to->node != 0) {
        // FIXME: recomputation is bad at this place
        to->max_depth = to->depth;
        for (size_t i = 0; i < to->num_next; i++) {
            const Scope* next = to->next[i];
            if (next->max_depth > to->max_depth) {
                to->max_depth = next->max_depth;
            }
        }
    }
    
    if (from->node != 0) {
        remove_scope_node(circuit, from->node);
        from->node = 0;
    }
}

void remove_scope(Circuit* circuit, Scope* scope) {
    //api_expect(circuit->phase == ENCODED || circuit->phase == PROPAGATION, "circuit must be encoded first\n");
    assert(vector_count(scope->vars) == 0);
    assert(scope != circuit->top_level);
    
    // Need to get previous scope
    Scope* previous = scope->prev;
    
    if (scope->num_next == 0) {
        // was last scope
        unlink_scope(previous, scope);
        free_scope(circuit, scope);
        return;
    }
    
    // Merge scopes if they are in quantifier prefix
    Scope* next = scope->next[0];
    assert(next != NULL);
    if (scope->num_next == 1 && next->node == 0) {
        assert(previous->qtype == next->qtype);
        
        // Copy variables from next to previous
        for (size_t i = 0; i < vector_count(next->vars); i++) {
            Var* var = vector_get(next->vars, i);
            vector_add(previous->vars, var);
            var->scope = previous;
        }
        vector_reset(next->vars);
        unlink_scope(previous, scope);
        unlink_scope(scope, next);
        while (next->num_next > 0) {
            Scope* after_next = next->next[0];
            unlink_scope(next, after_next);
            link_scope(previous, after_next);
        }
        
        // Clean up
        free_scope(circuit, scope);
        free_scope(circuit, next);
    } else {
        // we are not in the quantifier prefix
        // we remove the current scope and just link the next scopes
        unlink_scope(previous, scope);
        while (scope->num_next > 0) {
            Scope* next_scope = scope->next[0];
            unlink_scope(scope, next_scope);
            link_scope(previous, next_scope);
        }
        free_scope(circuit, scope);
    }
}

static Scope* create_scope(Circuit* circuit, quantifier_type qtype, bool in_prefix) {
    if (qtype == QUANT_FREE) {
        api_expect(in_prefix, "free scope is only allowed in prefix\n");
        return circuit->top_level;
    }
    assert(qtype == QUANT_EXISTS || qtype == QUANT_FORALL);
    
    if (in_prefix && circuit->previous_scope && circuit->previous_scope->qtype == qtype) {
        return circuit->previous_scope;
    }
    
    Scope* s = malloc(sizeof(Scope));
    s->circuit = circuit;
    s->scope_id = 0;
    s->depth = 0;
    s->max_depth = 0;
    s->qtype = qtype;
    s->vars = vector_init();
    s->num_next = 0;
    s->next = NULL;
    s->node = 0;
    s->prev = NULL;
    
    if (in_prefix) {
        s->scope_id = circuit->current_scope_id++;
        assert(s->scope_id != 0);
        if (circuit->current_scope_id > circuit->max_scope_id) {
            circuit->max_scope_id = circuit->current_scope_id;
        }
    }
    
    return s;
}

Scope* circuit_init_scope(Circuit* circuit, quantifier_type qtype) {
    Scope* s = create_scope(circuit, qtype, true);
    
    // Scope was created in the quantifier prefix, link to previous scope
    if (circuit->previous_scope != NULL && circuit->previous_scope != s) {
        link_scope(circuit->previous_scope, s);
    }
    circuit->previous_scope = s;
    return s;
}

ScopeNode* circuit_new_scope_node(Circuit* circuit, quantifier_type qtype, var_t node_id) {
    api_expect(qtype == QUANT_EXISTS || qtype == QUANT_FORALL, "unexpected quantifier type\n");
    ScopeNode* scope_node = malloc(sizeof(ScopeNode));
    init_node_shared(&scope_node->shared, node_id);
    scope_node->scope = create_scope(circuit, qtype, false);
    scope_node->scope->node = node_id;
    scope_node->sub = 0;
    scope_node->min_node = 0;
    
    import_literal(circuit, node_id);
    
    circuit->nodes[node_id] = (node*)scope_node;
    circuit->types[node_id] = NODE_SCOPE;
    
    return scope_node;
}

void circuit_set_scope_node(Circuit* circuit, ScopeNode* node, lit_t lit) {
    api_expect(node->sub == 0, "scope node already set\n");
    
    var_t var_id = lit_to_var(lit);
    if (var_id > circuit->max_num) {
        circuit_adjust(circuit, var_id);
    }
    
    node->sub = lit;
}

Gate* circuit_add_gate(Circuit* circuit, var_t gate_id, gate_type gate_type) {
    api_expect(gate_id > 0, "gate variables must be greater than 0\n");
    Gate* gate = malloc(sizeof(Gate));
    
    gate->type = gate_type;
    gate->size_inputs = 2;
    gate->num_inputs = 0;
    gate->inputs = calloc(sizeof(lit_t), gate->size_inputs);
    gate->conflict = false;
    gate->reachable = false;
    gate->keep = false;
    gate->negation = 0;
    gate->min_node = 0;
    gate->owner = 0;
    
    init_node_shared(&gate->shared, gate_id);
    
    import_literal(circuit, gate_id);
    
    circuit->nodes[gate_id] = (node*)gate;
    circuit->types[gate_id] = NODE_GATE;
    
    return gate;
}

static void remove_gate_input(Gate* gate, size_t pos) {
    for (size_t i = pos + 1; i < gate->num_inputs; i++) {
        gate->inputs[i - 1] = gate->inputs[i];
    }
    gate->num_inputs--;
}

static void remove_input_from_gate(Gate* gate, lit_t input) {
    assert(gate->num_inputs > 0);
    
    size_t k = 0;
    for (size_t i = 0; i < gate->num_inputs; i++) {
        gate->inputs[k] = gate->inputs[i];
        if (gate->inputs[i] != input) {
            k++;
        }
    }
    
    assert(k == gate->num_inputs - 1);
    gate->num_inputs = k;
}


/**
 * Checks if lit is already contained in gate.
 * Also, if -lit is contained, it marks the gate for propagation.
 */
static bool lit_check_contained(Gate* gate, int lit) {
    bool contained = false;
    for (size_t i = 0; i < gate->num_inputs; i++) {
        lit_t input = gate->inputs[i];
        if (input == lit) {
            contained = true;
        } else if (input == -lit) {
            logging_debug("conflict in gate %d (%d)\n", gate->shared.id, gate->shared.orig_id);
            // negation is contained in gate
            gate->conflict = true;
        }
    }
    return contained;
}

bool circuit_add_to_gate(Circuit* circuit, Gate* gate, lit_t lit) {
    var_t var_id = lit_to_var(lit);
    if (var_id > circuit->max_num) {
        circuit_adjust(circuit, var_id);
    }
    
    if (lit_check_contained(gate, lit)) {
        return false;
    }
    
    if (gate->num_inputs == gate->size_inputs) {
        // adjust buffer
        enlarge((void**)&gate->inputs, &gate->size_inputs, sizeof(lit_t), gate->num_inputs);
    }
    
    assert(gate->num_inputs < gate->size_inputs);
    gate->inputs[gate->num_inputs] = lit;
    gate->num_inputs++;
    return true;
}

static void free_gate(Gate* gate) {
    free(gate->inputs);
    free(gate);
}

static bool is_nnf(Circuit* circuit) {
    for (size_t i = 1; i <= circuit->max_num; i++) {
        if (circuit->types[i] == NODE_GATE) {
            Gate* gate = circuit->nodes[i];
            for (size_t j = 0; j < gate->num_inputs; j++) {
                lit_t input = gate->inputs[j];
                var_t input_var = lit_to_var(input);
                assert(circuit->types[input_var] == NODE_VAR || input > 0);
            }
        } else if (circuit->types[i] == NODE_SCOPE) {
            ScopeNode* node = circuit->nodes[i];
            var_t occ_var = lit_to_var(node->sub);
            assert(circuit->types[occ_var] == NODE_VAR || node->sub > 0);
        }
    }
    return true;
}

/*static void negate_gate(Gate* gate) {
    gate->type = circuit_negate_gate_type(gate->type);
    for (size_t i = 0; i < gate->num_inputs; i++) {
        gate->inputs[i] = -gate->inputs[i];
    }
}*/

static Gate* copy_and_negate(Circuit* circuit, Gate* gate) {
    Gate* copy = circuit_add_gate(circuit, circuit->max_num + 1, circuit_negate_gate_type(gate->type));
    for (size_t i = 0; i < gate->num_inputs; i++) {
        circuit_add_to_gate(circuit, copy, -gate->inputs[i]);
    }
    copy->shared.orig_id = -gate->shared.orig_id;
    copy->negation = gate->shared.id;
    gate->negation = copy->shared.id;
    return copy;
}

static void topological_sort_dfs(Circuit*, var_t*, var_t*, node_type*, lit_t);

static lit_t process_occurrence(Circuit* circuit, lit_t lit, var_t* new_ids, var_t* new_id, node_type* new_types) {
    const var_t marked = circuit->max_num + 1;  // special id to declare nodes as marked in DFS
    const var_t occ_var = lit_to_var(lit);
    
    assert(lit > 0 || circuit->types[occ_var] == NODE_VAR);
    
    topological_sort_dfs(circuit, new_ids, new_id, new_types, lit);
    const var_t new_occ_var = new_ids[occ_var];
    assert(new_occ_var > 0 && new_occ_var < marked);
    const lit_t new_lit = create_lit(new_occ_var, lit < 0);
    
    node_shared* other = circuit->nodes[occ_var];
    
    if (circuit->phase == BUILDING) {
        // after we have called reencoding once, the occurrence list is already built
        other->num_occ++;
    }
    
    // Update scope bit vector
    //bit_vector_update_or(node->scopes, other->scopes);
    return new_lit;
}

/**
 * Rearanges node id's such that id of parent node is larger than the nodes of
 * all its children. Does Depth-first search over the circuit data structure.
 *
 * Does also reassign scope id's during traversal.
 */
void topological_sort_dfs(Circuit* circuit, var_t* new_ids, var_t* new_id, node_type* new_types, lit_t lit) {
    const var_t marked = circuit->max_num + 1;  // special id to declare nodes as marked in DFS
    var_t node = lit_to_var(lit);
    assert(node > 0);
    assert(circuit->nodes[node] != NULL);
    api_expect(new_ids[node] != marked, "circuit is cyclic\n");
    
    if (new_ids[node] != 0) {
        return;
    }
    
    if (circuit->types[node] == NODE_VAR) {
        // Leaf, stop DFS
        const var_t new_node = (*new_id)++;
        new_ids[node] = new_node;
        new_types[new_node] = NODE_VAR;
        Var* var = circuit->nodes[node];
        var->shared.id = new_node;
    } else if (circuit->types[node] == NODE_SCOPE) {
        api_expect(new_ids[node] == 0, "quantified subformula can only be used once\n");
        new_ids[node] = marked;
        ScopeNode* scope_node = circuit->nodes[node];
        scope_node->min_node = *new_id;
        
        // Adjust scope pointer
        Scope* last_scope = circuit->previous_scope;
        link_scope(last_scope, scope_node->scope);
        circuit->previous_scope = scope_node->scope;
        
        // Assign scope id
        //assert(scope_node->scope->scope_id == 0);
        scope_node->scope->scope_id = circuit->current_scope_id++;
        if (circuit->current_scope_id > circuit->max_scope_id) {
            circuit->max_scope_id = circuit->current_scope_id;
        }
        scope_node->scope->depth = scope_node->scope->max_depth = circuit->current_depth++;
        if (circuit->current_depth > circuit->max_depth) {
            circuit->max_depth = circuit->current_depth;
        }
        
        scope_node->sub = process_occurrence(circuit, scope_node->sub, new_ids, new_id, new_types);
        
        const var_t new_node = (*new_id)++;
        new_ids[node] = new_node;
        new_types[new_node] = NODE_SCOPE;
        scope_node->shared.id = new_node;
        scope_node->scope->node = new_node;
        
        // Reset pointer to previous scope
        circuit->previous_scope = last_scope;
        circuit->current_depth--;
        for (size_t i = 0; i < scope_node->scope->num_next; i++) {
            const Scope* next = scope_node->scope->next[i];
            if (next->max_depth > scope_node->scope->max_depth) {
                scope_node->scope->max_depth = next->max_depth;
            }
        }
    } else {
        assert(circuit->types[node] == NODE_GATE);
        new_ids[node] = marked;
        Gate* gate = circuit->nodes[node];
        gate->min_node = *new_id;
        gate->reachable = true;
        for (size_t i = 0; i < gate->num_inputs; i++) {
            gate->inputs[i] = process_occurrence(circuit, gate->inputs[i], new_ids, new_id, new_types);
        }
        const var_t new_node = (*new_id)++;
        new_ids[node] = new_node;
        new_types[new_node] = NODE_GATE;
        gate->shared.id = new_node;
    }
}

static int compare_nodes(const void* lhs, const void* rhs) {
    node_shared** lhs_shared = (node_shared**)lhs;
    node_shared** rhs_shared = (node_shared**)rhs;
    
    if (*lhs_shared == NULL) {
        return 1;
    }
    if (*rhs_shared == NULL) {
        return -1;
    }
    
    assert(lhs_shared != NULL && rhs_shared != NULL);
    
    return (*lhs_shared)->id - (*rhs_shared)->id;
}

/**
 * Transforms circuit to negation normal form (NNF):
 * - Once we hit a negated gate, we append the negation to the list of nodes
 * - Double negation is handeled by returning the original gate
 * - This function can produce orphaned gates
 */
static void circuit_to_nnf(Circuit* circuit) {
    
    // check of output is negated
    if (circuit->output < 0) {
        var_t var = lit_to_var(circuit->output);
        assert(circuit->types[var] == NODE_GATE);
        
        Gate* other = circuit->nodes[var];
        if (other->negation != 0) {
            assert(other->negation > 0);
            circuit->output = other->negation;
        } else {
            Gate* negation = copy_and_negate(circuit, other);
            circuit->output = create_lit(negation->shared.id, false);
        }
    }
    
    for (size_t i = 1; i <= circuit->max_num; i++) {
        if (circuit->types[i] != NODE_GATE) {
            continue;
        }
        Gate* gate = circuit->nodes[i];
        for (size_t j = 0; j < gate->num_inputs; j++) {
            lit_t lit = gate->inputs[j];
            var_t var = lit_to_var(lit);
            if (lit > 0 || circuit->types[var] == NODE_VAR) {
                continue;
            }
            
            // Have to negate gate corresponding to lit
            Gate* other = circuit->nodes[var];
            if (other->negation != 0) {
                assert(other->negation > 0);
                gate->inputs[j] = other->negation;
                continue;
            }
            
            Gate* negation = copy_and_negate(circuit, other);
            gate->inputs[j] = create_lit(negation->shared.id, false);
        }
    }
}

static void remove_empty_scopes(Circuit* circuit, Scope* scope) {
    size_t i = 0;
    while (i < scope->num_next) {
        Scope* next = scope->next[i];
        remove_empty_scopes(circuit, next);
        if (vector_count(next->vars) != 0) {
            i++;
            continue;
        }
        // next scope is empty, remove it
        remove_scope(circuit, next);
    }
}

/**
 * Modfies circuit such that there is a strict alternation between AND and OR
 * gates, i.e., combines consecutive AND/OR gates.
 *
 * Assumes circuit in NNF.
 */
void circuit_flatten_gates(Circuit* circuit) {
    api_expect(is_nnf(circuit), "assumes circuit in NNF\n");
    for (size_t i = 0; i < circuit->max_num; i++) {
        const node_type type = circuit->types[i];
        if (type != NODE_GATE) {
            continue;
        }
        
        Gate* gate = circuit->nodes[i];
        size_t j = 0;
        while (j < gate->num_inputs) {
            lit_t input = gate->inputs[j];
            var_t input_var = lit_to_var(input);
            if (circuit->types[input_var] != NODE_GATE) {
                j++;
                continue;
            }
            Gate* other_gate = circuit->nodes[input_var];
            if (gate->type != other_gate->type) {
                j++;
                continue;
            }
            remove_gate_input(gate, j);
            if (circuit->phase != BUILDING) {
                other_gate->shared.num_occ--;
            }
            for (size_t k = 0; k < other_gate->num_inputs; k++) {
                lit_t other_input = other_gate->inputs[k];
                const bool added = circuit_add_to_gate(circuit, gate, other_input);
                if (added && circuit->phase != BUILDING) {
                    node_shared* node = circuit->nodes[lit_to_var(other_input)];
                    node->num_occ++;
                }
            }
        }
    }
}

static void remove_unnecessary_quantifier_scopes(Circuit* circuit) {
    api_expect(is_nnf(circuit), "need to transform to NNF first\n");
    assert(circuit->output > 0);
    
    if (circuit->types[circuit->output] != NODE_SCOPE) {
        return;
    }
    
    // get last quantifier in prenex
    Scope* last_quant = NULL;
    for (Scope* scope = circuit->top_level; scope != NULL; scope = circuit_next_scope_in_prefix(scope)) {
        last_quant = scope;
    }
    assert(last_quant != NULL);
    
    while (circuit->types[circuit->output] == NODE_SCOPE) {
        ScopeNode* node = circuit->nodes[circuit->output];
        Scope* scope = node->scope;
        assert(scope->prev == NULL);
        link_scope(last_quant, scope);
        last_quant = scope;
        scope->node = 0;
        circuit->output = node->sub;
        assert(node->sub > 0);
        // node is circuit output, so it cannot be referenced twice
        free_scope_node(circuit, node);
    }
}

/**
 * In order to have efficient evaluation, we assume some properties about the
 * encoding of the circuit that are guaranteed by this function:
 * - The nodes (variables/gates) are sorted topological, i.e., for every gate n
 *   and every input m of n, it holds that m < n
 * - The inputs to gates are sorted (descending) by their id (after reassigning)
 * - Transforms circuit to Negation Normal Form (NNF), i.e., negations appear
 *   only before variables
 */
void circuit_reencode(Circuit* circuit) {
    api_expect(circuit_check_all_nodes_defined(circuit), "there were undefined gates\n");
    circuit_to_nnf(circuit);
    remove_empty_scopes(circuit, circuit->top_level);
    //circuit_flatten_gates(circuit);
    remove_unnecessary_quantifier_scopes(circuit);
    
    // Init temporary data structures
    var_t* new_ids = calloc(circuit->size + 1, sizeof(var_t));  // maps old id's to new id's
    node_type* new_types = calloc(circuit->size + 1, sizeof(node_type));
    var_t new_id = 1;
    circuit->current_scope_id = circuit->max_scope_id = 1;
    circuit->current_depth = circuit->max_depth = 1;
    
    // Iterate over quantifier prefix and assign consecutive scope id's
    for (Scope* scope = circuit->top_level; scope != NULL; scope = circuit_next_scope_in_prefix(scope)) {
        scope->scope_id = circuit->current_scope_id++;
        if (circuit->current_scope_id > circuit->max_scope_id) {
            circuit->max_scope_id = circuit->current_scope_id;
        }
        scope->depth = circuit->current_depth++;
        if (circuit->current_depth > circuit->max_depth) {
            circuit->max_depth = circuit->current_depth;
        }
        circuit->previous_scope = scope;
    }
    
    // Start DFS at the output
    topological_sort_dfs(circuit, new_ids, &new_id, new_types, circuit->output);
    
    // adjust occurrence for output gate
    //node_shared* output_node = circuit->nodes[lit_to_var(circuit->output)];
    //output_node->num_occ++;
    
    // clean-up nodes that are not reachable
    for (size_t i = circuit->max_num; i > 0; i--) {
        if (circuit->types[i] == NODE_GATE) {
            //Gate* gate = circuit->nodes[i];
        }
        if (circuit->types[i] == 0) {
            assert(circuit->nodes[i] == NULL);
            continue;
        }
        if (new_ids[i] != 0) {
            continue;
        }
        // FIXME: correct clean-up for node, currently it leaks memory
        if (circuit->types[i] == NODE_VAR) {
            remove_var(circuit, i);
        } else if (circuit->types[i] == NODE_GATE) {
            remove_gate(circuit, i);
        } else if (circuit->types[i] == NODE_SCOPE) {
            assert(false);
            free_scope_node(circuit, circuit->nodes[i]);
        }
        circuit->nodes[i] = NULL;
    }
    
    var_t new_max = new_ids[circuit->output];
    assert(new_max > 0 && new_max <= circuit->max_num);
    
    qsort(circuit->nodes + 1, circuit->max_num, sizeof(node*), compare_nodes);
    
    circuit->output = create_lit_from_value(new_ids[lit_to_var(circuit->output)], circuit->output);
    circuit->max_num = new_max;
    
    free(circuit->types);
    circuit->types = new_types;
    
    free(new_ids);
    
    circuit->phase = ENCODED;
    
    circuit_normalize_quantifier(circuit);
    
    assert(circuit_check(circuit));
    assert(is_nnf(circuit));
}


// Value Propagation
void circuit_set_value(Circuit* circuit, var_t node, int value) {
    assert(node > 0 && node <= circuit->max_num);
    ((node_shared*)circuit->nodes[node])->value = value;
}

int circuit_get_value(Circuit* circuit, var_t node) {
    assert(node > 0 && node <= circuit->max_num);
    return ((node_shared*)circuit->nodes[node])->value;
}

void circuit_evaluate_max(Circuit* circuit, int max_value) {
    assert(max_value > 0 || max_value == EVALUATION_NO_MAX);
    const bool no_max = max_value == EVALUATION_NO_MAX;
    if (no_max) {
        max_value = 1;
    }
    
    for (size_t i = 1; i <= circuit->max_num; i++) {
        void* node = circuit->nodes[i];
        if (node == NULL) {
            continue;
        }
        const node_type type = circuit->types[i];
        if (type == NODE_VAR) {
            continue;
        } else if (type == NODE_SCOPE) {
            ScopeNode* scope_node = node;
            node_shared* occ_node = circuit->nodes[lit_to_var(scope_node->sub)];
            scope_node->shared.value = occ_node->value;
            continue;
        }
        
        assert(type == NODE_GATE);
        Gate* gate = node;
        int value = (gate->type == GATE_AND) ? max_value : -max_value;
        const int orig_value = gate->shared.value;
        if (gate->type == GATE_OR && orig_value > 0) {
            if (!no_max && orig_value < max_value) {
                continue;
            }
        } else if (gate->type == GATE_AND && orig_value < 0) {
            if (!no_max && orig_value > max_value) {
                continue;
            }
        }
        
        for (size_t j = 0; j < gate->num_inputs; j++) {
            const lit_t lit = gate->inputs[j];
            const var_t occ_var = lit_to_var(lit);
            assert(occ_var < i);
            const node_shared* occ_node = circuit->nodes[occ_var];
            int occ_val = occ_node->value;
            if (lit < 0) {
                occ_val = -occ_val;
            }
            if (!no_max && (occ_val > max_value || -occ_val > max_value)) {
                // absolute value is larger than max_value
                occ_val = 0;
            }
            
            if (gate->type == GATE_AND) {
                // undefined input
                if (value > 0 && occ_val == 0) {
                    value = 0;
                }
                
                // short circuit
                if (occ_val < 0) {
                    value = occ_val;
                    break;
                }
                if (gate->conflict) {
                    value = -max_value;
                    break;
                }
            } else {
                assert(gate->type == GATE_OR);
                
                // undefined input
                if (value < 0 && occ_val == 0) {
                    value = 0;
                }
                
                // short circuit
                if (occ_val > 0) {
                    value = occ_val;
                    break;
                }
                if (gate->conflict) {
                    value = max_value;
                    break;
                }
            }
        }
        
        // Do not increase absolute value if previous value had the same polarity
        if (orig_value < 0 && value < 0 && value < orig_value) {
            value = orig_value;
        } else if (orig_value > 0 && value > 0 && value > orig_value) {
            value = orig_value;
        }
        
        gate->shared.value = value;
    }
}

void circuit_evaluate(Circuit* circuit) {
    circuit_evaluate_max(circuit, EVALUATION_NO_MAX);
}

/**
 * Removes singleton gate from circuit.
 * Updates occurrence lists.
 */
static void remove_singleton_gate(Circuit* circuit, Gate* outer_gate, size_t pos, Gate* inner_gate) {
    assert(inner_gate->num_inputs == 1);
    logging_debug("asingleton gate %d (%d)\n", inner_gate->shared.id, inner_gate->shared.orig_id);
    
    const lit_t inner = outer_gate->inputs[pos];
    remove_input_from_gate(outer_gate, inner);
    
    const lit_t input = inner_gate->inputs[0];
    const var_t input_var = lit_to_var(input);
    const bool added = circuit_add_to_gate(circuit, outer_gate, input);
    node_shared* input_node = circuit->nodes[input_var];
    
    inner_gate->shared.num_occ--;
    if (inner_gate->shared.num_occ == 0) {
        // removal does not change occurrences of other vars, except when node was not added
        inner_gate->num_inputs = 0;
        remove_gate(circuit, inner_gate->shared.id);
        if (!added) {
            input_node->num_occ--;
        }
    } else if (added) {
        // we actually duplicated the input, hence, we have to increase occurrence
        input_node->num_occ++;
    }
    
    assert(circuit_check(circuit));
}

/**
 * Add all inputs to inner_gate to outer_gate and frees inner_gate.
 */
static void flatten_gates(Circuit* circuit, Gate* outer_gate, size_t pos, Gate* inner_gate) {
    assert(outer_gate->type == inner_gate->type);
    assert(inner_gate->shared.num_occ == 1);
    assert(outer_gate->inputs[pos] > 0);
    assert(lit_to_var(outer_gate->inputs[pos]) == inner_gate->shared.id);
    assert(outer_gate->num_inputs > 0);
    assert(inner_gate->num_inputs > 0);
    
    remove_gate_input(outer_gate, pos);
    
    for (size_t i = 0; i < inner_gate->num_inputs; i++) {
        const lit_t input = inner_gate->inputs[i];
        const bool added = circuit_add_to_gate(circuit, outer_gate, input);
        if (!added) {
            node_shared* input_node = circuit->nodes[lit_to_var(input)];
            input_node->num_occ--;
        }
    }
    inner_gate->shared.num_occ--;
    
    inner_gate->num_inputs = 0;
    remove_gate(circuit, inner_gate->shared.id);
}

static void detect_empty_scopes_recursively(Circuit* circuit, Scope* scope) {
    for (size_t i = 0; i < scope->num_next; i++) {
        detect_empty_scopes_recursively(circuit, scope->next[i]);
    }
    if (vector_count(scope->vars) == 0 && scope != circuit->top_level) {
        remove_scope(circuit, scope);
    }
}

Gate* circuit_is_gate(Circuit* circuit, lit_t lit) {
    var_t var = lit_to_var(lit);
    if (circuit->types[var] == NODE_GATE) {
        assert(circuit->nodes[var] != NULL);
        return circuit->nodes[var];
    }
    return NULL;
}

Var* circuit_is_var(Circuit* circuit, lit_t lit) {
    var_t var = lit_to_var(lit);
    if (circuit->types[var] == NODE_VAR) {
        assert(circuit->nodes[var] != NULL);
        return circuit->nodes[var];
    }
    return NULL;
}

/**
 * Normalizes the circuit data structure. Performs the following techniques:
 * - Flattening: and(and(a, b), c) => and(a, b, c) [Method checks that there is only one occurrence of and(a,b)]
 * - Singletons: and(a, or(b)) => and(a, b)
 */
static size_t circuit_normalize(Circuit* circuit) {

    size_t changes = 0;
    
    for (size_t var = 1; var <= circuit->max_num; var++) {
        const node_type type = circuit->types[var];
        
        if (type != NODE_GATE) {
            continue;
        }
        
        Gate* gate = circuit->nodes[var];
        for (size_t i = 0; i < gate->num_inputs; i++) {
            lit_t input = gate->inputs[i];
            Gate* inner_gate = circuit_is_gate(circuit, input);
            if (inner_gate == NULL || inner_gate->num_inputs == 0) {
                continue;
            }
            if (inner_gate->num_inputs == 1) {
                remove_singleton_gate(circuit, gate, i, inner_gate);
                changes++;
            } else if (gate->type == inner_gate->type && inner_gate->shared.num_occ == 1) {
                flatten_gates(circuit, gate, i, inner_gate);
                changes++;
            }
        }
    }
    return changes;
}

static void update_polarity(Var* var, lit_t occurrence) {
    assert(occurrence != 0);
    
    if (var->polarity == POLARITY_UNDEFINED) {
        if (occurrence > 0) {
            var->polarity = POLARITY_POS;
        } else {
            var->polarity = POLARITY_NEG;
        }
    } else if (var->polarity == POLARITY_NEG && occurrence > 0) {
        var->polarity = POLARITY_NONE;
    } else if (var->polarity == POLARITY_POS && occurrence < 0) {
        var->polarity = POLARITY_NONE;
    } else {
        assert(var->polarity == POLARITY_NONE || var->polarity == POLARITY_POS && occurrence > 0 || var->polarity == POLARITY_NEG && occurrence < 0);
    }
}

static void circuit_compute_polarities(Circuit* circuit) {
    
    for (size_t i = 1; i <= circuit->max_num; i++) {
        if (circuit->types[i] == NODE_GATE) {
            Gate* gate = circuit->nodes[i];
            for (size_t j = 0; j < gate->num_inputs; j++) {
                lit_t gate_input = gate->inputs[j];
                var_t gate_var = lit_to_var(gate_input);
                if (circuit->types[gate_var] != NODE_VAR) {
                    continue;
                }
                Var* var = circuit->nodes[gate_var];
                update_polarity(var, gate_input);
            }
        } else if (circuit->types[i] == NODE_SCOPE) {
            ScopeNode* node = circuit->nodes[i];
            var_t sub_var = lit_to_var(node->sub);
            if (circuit->types[sub_var] != NODE_VAR) {
                continue;
            }
            Var* var = circuit->nodes[sub_var];
            update_polarity(var, node->sub);
        }
    }
    
    for (size_t i = 0; i < vector_count(circuit->vars); i++) {
        Var* var = vector_get(circuit->vars, i);
        if (var->removed) {
            continue;
        }
        polarity_type polarity = var->polarity;
        
        int value = 0;
        if (polarity == POLARITY_NEG) {
            value = -1;
            logging_debug("variable %d appears only negatively\n", var->shared.orig_id);
        } else if (polarity == POLARITY_POS) {
            value = 1;
            logging_debug("variable %d appears only positively\n", var->shared.orig_id);
        }
        
        if (var->scope->qtype == QUANT_FORALL) {
            value = -value;
        }
        
        if (value != 0) {
            circuit_set_value(circuit, var->shared.id, value);
        } else {
            // Reset if called again
            var->polarity = POLARITY_UNDEFINED;
        }
    }
}

static void get_forced_variables(Circuit* circuit) {
    Gate* root = circuit->nodes[circuit->output];
    for (size_t i = 0; i < root->num_inputs; i++) {
        const lit_t lit = root->inputs[i];
        const var_t var = lit_to_var(lit);
        const Var* var_node = circuit_is_var(circuit, lit);
        if (var_node != NULL) {
            //printf("Value of %d must be %d\n", var, lit);
            if (var_node->scope->qtype == QUANT_FORALL) {
                circuit_set_value(circuit, var, lit > 0 ? -1 : 1);
            } else {
                circuit_set_value(circuit, var, lit > 0 ? 1 : -1);
            }
        }
    }
}

void remove_var(Circuit* circuit, var_t var_id) {
    //api_expect(circuit->phase == ENCODED || circuit->phase == PROPAGATION, "circuit must be encoded first\n");
    api_expect(var_id > 0, "variables must be greater than zero\n");
    api_expect(circuit->types[var_id] == NODE_VAR, "not a variable\n");
    
    Var* var = circuit->nodes[var_id];
    assert(var != NULL);
    
    logging_debug("Remove variable %d (%d), value %d\n", var_id, var->shared.orig_id, var->shared.value);
    
    // Remove var from scope
    Scope* var_scope = var->scope;
    bool contained = vector_remove(var_scope->vars, var);
    assert(contained);
    if (vector_count(var_scope->vars) == 0 && var_scope != circuit->top_level) {
        logging_debug("scope %d became empty\n", var->scope->scope_id);
        remove_scope(circuit, var_scope);
    }
    
    // Remove node from circuit
    //remove_node(circuit, &var->shared);
    
    var->removed = true;
    circuit->nodes[var_id] = NULL;
    circuit->types[var_id] = 0;
    //free(var); not freed since still contained in circuit->vars
}

void remove_gate(Circuit* circuit, var_t gate_id) {
    //api_expect(circuit->phase == ENCODED || circuit->phase == PROPAGATION, "circuit must be encoded first\n");
    api_expect(gate_id > 0, "variables must be greater than zero\n");
    api_expect(circuit->types[gate_id] == NODE_GATE, "not a gate\n");
    
    Gate* gate = circuit->nodes[gate_id];
    assert(gate != NULL);
    
    logging_debug("Remove gate %d (%d), value %d\n", gate_id, gate->shared.orig_id, gate->shared.value);
    
    for (size_t i = 0; i < gate->num_inputs; i++) {
        lit_t input = gate->inputs[i];
        var_t input_var = lit_to_var(input);
        node_shared* node = circuit->nodes[input_var];
        if (node == NULL) {
            continue;
        }
        if (circuit->phase == ENCODED || circuit->phase == PROPAGATION) {
            node->num_occ--;
        }
    }
    
    gate->num_inputs = 0;
    
    // Remove node from circuit
    if (lit_to_var(circuit->output) == gate->shared.id) {
        // Gate serves as output, leave it
        assert(gate->shared.value != 0);
        if (gate->shared.value > 0) {
            gate->type = GATE_AND;
        } else {
            gate->type = GATE_OR;
        }
        assert(gate->shared.value > 0 && gate->type == GATE_AND
            || gate->shared.value < 0 && gate->type == GATE_OR);
        //bit_vector_reset(gate->shared.scopes);
        //bit_vector_add(gate->shared.scopes, 1);
        gate->conflict = false;
    } else {
        circuit->nodes[gate_id] = NULL;
        circuit->types[gate_id] = 0;
        free_gate(gate);
    }
}

/**
 * Works in two phases, in the first phase, the occurrences for every gate
 * in subtree is reduced, in the second phase nodes with empty occurrences are
 * removed.
 */
static void remove_gates_recursive(Circuit* circuit, var_t node_var) {
    assert(circuit->types[node_var] == NODE_GATE);
    
    Gate* gate = circuit->nodes[node_var];
    if (gate->shared.num_occ > 0) {
        return;
    }
    circuit->nodes[node_var] = NULL;
    circuit->types[node_var] = 0;
    
    for (size_t i = 0; i < gate->num_inputs; i++) {
        const var_t input_var = lit_to_var(gate->inputs[i]);
        assert(circuit->types[input_var] != NODE_SCOPE);
        node_shared* node = circuit->nodes[input_var];
        if (node != NULL) {
            node->num_occ--;
        }
        if (circuit->types[input_var] != NODE_GATE) {
            //assert(circuit->types[input_var] == NODE_VAR);
            continue;
        }
        assert(node != NULL);
        remove_gates_recursive(circuit, input_var);
    }
    free_gate(gate);
}

static size_t remove_orphans(Circuit* circuit) {
    size_t removed = 0;
    for (size_t i = 1; i <= circuit->max_num; i++) {
        node_type type = circuit->types[i];
        if (type == NODE_GATE) {
            Gate* gate = circuit->nodes[i];
            size_t k = 0;
            for (size_t j = 0; j < gate->num_inputs; j++) {
                lit_t gate_input = gate->inputs[j];
                var_t gate_var = lit_to_var(gate_input);
                gate->inputs[k] = gate->inputs[j];
                if (circuit->nodes[gate_var] != NULL) {
                    k++;
                } else {
                    removed++;
                }
            }
            assert(k <= gate->num_inputs);
            gate->num_inputs = k;
        } else if (type == NODE_SCOPE) {
            ScopeNode* scope_node = circuit->nodes[i];
            var_t var = lit_to_var(scope_node->sub);
            assert(circuit->nodes[var] != NULL);
        }
    }
    return removed;
}

static bool circuit_propagate(Circuit* circuit) {
    unsigned num_propagations = 0;
    circuit_evaluate(circuit);
    // Remove gates and variables whose values were determined
    for (var_t i = 1; i <= circuit->max_num; i++) {
        if (circuit->nodes[i] == NULL) {
            continue;
        }
        node_shared* node = circuit->nodes[i];
        if (node->value == 0) {
            continue;
        }
        
        if (i != lit_to_var(circuit->output)) {
            num_propagations++;
        }
        
        if (circuit->types[i] == NODE_VAR) {
            remove_var(circuit, i);
        } else if (circuit->types[i] == NODE_SCOPE) {
            remove_scope_node(circuit, i);
        } else {
            assert(circuit->types[i] == NODE_GATE);
            remove_gate(circuit, i);
        }
    }
    
    num_propagations += remove_orphans(circuit);
    
    logging_info("%d propagations\n", num_propagations);
    return num_propagations > 0;
}


/**
 * Implements the following preprocessing techniques:
 * - Propagation of fixed variables (unit clauses)
 * - Single polarity variables
 * @see circuit_normalize
 */
void circuit_preprocess(Circuit* circuit) {
    api_expect(circuit->phase == ENCODED, "circuit must be encoded\n");
    circuit->phase = PROPAGATION;
    
    bool changed;
    do {
        changed = false;
        changed |= circuit_normalize(circuit) > 0;
        assert(circuit_check(circuit));
        detect_empty_scopes_recursively(circuit, circuit->top_level);
        circuit_compute_polarities(circuit);
        get_forced_variables(circuit);
        
        changed |= circuit_propagate(circuit);
        assert(circuit_check(circuit));
        
        circuit_reencode(circuit);
    } while (changed);
}


static void normalize_quantifier_recursive(Circuit*, Scope*);
static void normalize_quantifier_recursive(Circuit* circuit, Scope* scope) {
    size_t i = 0;
    while (i < scope->num_next) {
        Scope* child = scope->next[i];
        normalize_quantifier_recursive(circuit, child);
        
        if (scope->qtype != child->qtype) {
            i++;
        } else {
            // we merge the child into parent
            merge_scopes(circuit, child, scope);
        }
    }
}

/**
 * Ensures that there is a strict quantifier alternation along all branches in
 * the quantifier tree.
 *
 * Implementation: when there are two consecutive quantifiers, merge the child
 * into the parent
 */
void circuit_normalize_quantifier(Circuit* circuit) {
    normalize_quantifier_recursive(circuit, circuit->top_level);
}


/**
 * Converts the circuit into prenex form
 *
 * Implementation: since we have ensured that quantifier levels are strictly
 * alternating, we collapse the scopes level by level
 */
void circuit_to_prenex(Circuit* circuit) {
    // Get last scope in quantifier prefix
    Scope* last_in_prefix = circuit->top_level;
    while (circuit_next_scope_in_prefix(last_in_prefix)) {
        last_in_prefix = circuit_next_scope_in_prefix(last_in_prefix);
    }
    
    while (last_in_prefix->num_next > 0) {
        assert(last_in_prefix->num_next >= 1);
        
        // Create a new scope in prefix with negated quantifier type
        Scope* new_last_in_prefix = circuit_init_scope(circuit, circuit_negate_quantifier_type(last_in_prefix->qtype));
        
        // Note: The following code depends on the implementation detail that
        // scopes are added to the last position of scope->next
        
        while (last_in_prefix->num_next > 1) {
            Scope* not_in_prefix = last_in_prefix->next[0];
            assert(not_in_prefix->qtype == new_last_in_prefix->qtype);
            assert(not_in_prefix->node != 0);
            unlink_scope(last_in_prefix, not_in_prefix);
            link_scope(new_last_in_prefix, not_in_prefix);
            merge_scopes(circuit, not_in_prefix, new_last_in_prefix);
            assert(vector_count(not_in_prefix->vars) == 0);
            free_scope(circuit, not_in_prefix);
        }
        assert(last_in_prefix->next[0] == new_last_in_prefix);
        last_in_prefix = new_last_in_prefix;
    }
}

static void move_variable(Var* var, Scope* from, Scope* to) {
    assert(vector_contains(from->vars, var));
    assert(var->scope == from);
    
    vector_remove(from->vars, var);
    vector_add(to->vars, var);
    var->scope = to;
}

static Var* copy_variable(Circuit* circuit, Var* variable, Scope* new_scope) {
    Var* copy = circuit_new_var(circuit, new_scope, circuit->max_num + 1);
    copy->shared.orig_id = variable->shared.orig_id;
    return copy;
}

static Gate* copy_gate(Circuit* circuit, Gate* gate) {
    Gate* copy = circuit_add_gate(circuit, circuit->max_num + 1, gate->type);
    for (size_t i = 0; i < gate->num_inputs; i++) {
        circuit_add_to_gate(circuit, copy, gate->inputs[i]);
    }
    return copy;
}

static lit_t replace_variables_in_subtree(Circuit* circuit, map* variable_mapping, const var_t scope_node_id, bool shared, const lit_t subtree) {
    var_t subtree_var = lit_to_var(subtree);
    node_type type = circuit->types[subtree_var];
    if (type == NODE_VAR) {
        assert(!map_contains(variable_mapping, subtree_var));
        return subtree;
    } else if (type == NODE_SCOPE) {
        ScopeNode* scope_node = circuit->nodes[subtree_var];
        assert(shared == false);
        lit_t new_sub = replace_variables_in_subtree(circuit, variable_mapping, scope_node_id, shared, scope_node->sub);
        assert(new_sub == scope_node->sub);  // TODO: check if this is actually true
        scope_node->sub = new_sub;
        
        //node_shared* sub_node = circuit->nodes[lit_to_var(scope_node->sub)];
        //bit_vector_free(scope_node->shared.influences);
        //scope_node->shared.influences = bit_vector_init(0, circuit->max_num);
        //bit_vector_update_or(scope_node->shared.influences, sub_node->influences);
        
        return subtree;
    } else {
        assert(type == NODE_GATE);
        Gate* gate = circuit->nodes[subtree_var];
        shared = shared || gate->shared.num_occ > 1;
        //bit_vector_free(gate->shared.influences);
        //gate->shared.influences = bit_vector_init(0, circuit->max_num);
        for (size_t i = 0; i < gate->num_inputs; i++) {
            const lit_t gate_input = gate->inputs[i];
            const var_t gate_input_var = lit_to_var(gate_input);
            node_shared* old_input = circuit->nodes[gate_input_var];
            node_shared* transformed_input = map_get(variable_mapping, gate_input_var);
            lit_t new_input;
            if (transformed_input != NULL) {
                // may be variable or previously transformed node
                new_input = create_lit_from_value(transformed_input->id, gate_input);
            } else {
                // recursively replace otherwise
                new_input = replace_variables_in_subtree(circuit, variable_mapping, scope_node_id, shared, gate->inputs[i]);
                transformed_input = circuit->nodes[lit_to_var(new_input)];
            }
            assert(transformed_input != NULL);
            
            if (new_input != gate_input) {
                assert(old_input != NULL);
                assert(transformed_input != NULL);
                assert(old_input != transformed_input);
                
                // copy on write
                if (shared && gate->owner != scope_node_id) {
                    assert(gate->owner == 0);
                    Gate* copy = copy_gate(circuit, gate);
                    map_add(variable_mapping, gate->shared.id, &copy->shared);
                    //copy->shared.influences = bit_vector_init(0, circuit->max_num);
                    //bit_vector_update_or(copy->shared.influences, gate->shared.influences);
                    copy->owner = scope_node_id;
                    for (size_t j = 0; j < copy->num_inputs; j++) {
                        // add +1 to occurrences since gate is copied
                        lit_t input = copy->inputs[j];
                        node_shared* node = circuit->nodes[lit_to_var(input)];
                        if (i != j) {
                            node->num_occ++;
                        } else {
                            transformed_input->num_occ++;
                        }
                    }
                    copy->shared.orig_id = gate->shared.orig_id;
                    assert(gate->num_inputs == copy->num_inputs);
                    gate = copy;
                } else {
                    // gate remains, we just swap replace inputs
                    old_input->num_occ--;
                    transformed_input->num_occ++;
                }
            }
            
            gate->inputs[i] = new_input;
            //node_shared* sub_node = circuit->nodes[lit_to_var(gate->inputs[i])];
            //bit_vector_update_or(gate->shared.influences, sub_node->influences);
        }
        return create_lit(gate->shared.id, false);
    }
}

static void circuit_compute_variable_influence_dfs(Circuit* circuit, lit_t node_id) {
    const var_t node_var = lit_to_var(node_id);
    const node_type type = circuit->types[node_var];
    
    node_shared* node = circuit->nodes[node_var];
    assert(node != NULL);
    
    if (node->dfs_processed) {
        return;
    }
    
    if (type == NODE_VAR) {
        return;
    } else if (type == NODE_GATE) {
        Gate* gate = circuit->nodes[node_var];
        for (size_t j = 0; j < gate->num_inputs; j++) {
            const lit_t gate_input = gate->inputs[j];
            circuit_compute_variable_influence_dfs(circuit, gate_input);
            const var_t var = lit_to_var(gate_input);
            node_shared* occ_node = circuit->nodes[var];
            assert(occ_node->influences != NULL);
            bit_vector_update_or(node->influences, occ_node->influences);
        }
        gate->shared.dfs_processed = true;
    } else {
        assert(type == NODE_SCOPE);
        ScopeNode* scope_node = circuit->nodes[node_var];
        circuit_compute_variable_influence_dfs(circuit, scope_node->sub);
        const var_t var = lit_to_var(scope_node->sub);
        node_shared* occ_node = circuit->nodes[var];
        assert(occ_node->influences != NULL);
        bit_vector_update_or(node->influences, occ_node->influences);
        scope_node->shared.dfs_processed = true;
    }
}

static void circuit_compute_variable_influence(Circuit* circuit) {
    const size_t max_var = circuit->num_vars;
    
    // Compute which variable influences which gate
    for (size_t i = 1; i <= circuit->max_num; i++) {
        node_shared* node = circuit->nodes[i];
        if (node == NULL) {
            continue;
        }
        //assert(node->influences == NULL);
        bit_vector_free(node->influences);
        node->influences = bit_vector_init(0, max_var);
        node->dfs_processed = false;
        
        const node_type type = circuit->types[i];
        
        if (type == NODE_VAR) {
            Var* var = circuit->nodes[i];
            assert(var->var_id < max_var);
            bit_vector_add(node->influences, var->var_id);
        } else if (type == NODE_GATE) {
            Gate* gate = circuit->nodes[i];
            gate->owner = 0;
        }
    }
    
    circuit_compute_variable_influence_dfs(circuit, circuit->output);
}

static void apply_miniscoping(Circuit* circuit, Scope* scope, var_t node, var_t* partitions) {
    if (circuit->types[node] != NODE_GATE) {
        return;
    }
    
    circuit_compute_variable_influence(circuit);
    
    Gate* gate = circuit->nodes[node];
    
    if (gate->shared.num_occ > 1) {
        // Do not split if gate is shared
        return;
    }
    
    if ((gate->type == GATE_AND && scope->qtype == QUANT_FORALL) || (gate->type == GATE_OR && scope->qtype == QUANT_EXISTS)) {
        // create a scope node for every gate input
        for (size_t i = 0; i < gate->num_inputs; i++) {
            lit_t gate_input = gate->inputs[i];
            ScopeNode* scope_node = circuit_new_scope_node(circuit, scope->qtype, circuit->max_num + 1);
            map* replacement = map_init();
            node_shared* gate_node = circuit->nodes[lit_to_var(gate_input)];
            for (size_t k = 0; k < vector_count(scope->vars); k++) {
                Var* var = vector_get(scope->vars, k);
                if (bit_vector_contains(gate_node->influences, var->var_id)) {
                    // copy variable and add it to newly created scope
                    Var* copy = copy_variable(circuit, var, scope_node->scope);
                    //copy->shared.influences = bit_vector_init(0, circuit->num_vars);
                    //bit_vector_add(copy->shared.influences, copy->var_id);
                    map_add(replacement, var->shared.id, &copy->shared);
                }
            }
            if (vector_count(scope_node->scope->vars) == 0) {
                free_scope_node(circuit, scope_node);
                continue;
            }
            
            // connect scope_node to circuit
            gate->inputs[i] = scope_node->shared.id;
            scope_node->sub = replace_variables_in_subtree(circuit, replacement, scope_node->shared.id, false, gate_input);
            scope_node->shared.num_occ = 1;
            if (scope_node->sub != gate_input) {
                node_shared* sub_node = circuit->nodes[lit_to_var(scope_node->sub)];
                sub_node->num_occ++;
                node_shared* old_sub_node = circuit->nodes[lit_to_var(gate_input)];
                old_sub_node->num_occ--;
            }
            //scope_node->shared.influences = bit_vector_init(0, circuit->max_num);
            //node_shared* sub_node = circuit->nodes[lit_to_var(scope_node->sub)];
            //bit_vector_update_or(scope_node->shared.influences, sub_node->influences);
            
            map_free(replacement);
        }
        
        assert(scope->num_next == 0);
        
        size_t num_vars = vector_count(scope->vars);
        while (num_vars > 0) {
            Var* var = vector_get(scope->vars, 0);
            remove_var(circuit, var->shared.id);
            num_vars--;
        }
        // remove no longer reachable nodes
        for (size_t i = 1; i <= circuit->max_num; i++) {
            node_shared* other_node = circuit->nodes[i];
            if (other_node == NULL) {
                continue;
            }
            if (other_node->num_occ == 0 && other_node->id != lit_to_var(circuit->output)) {
                remove_gates_recursive(circuit, other_node->id);
            }
        }
    } else {
        // check if there are influences
        for (size_t i = 0; i < gate->num_inputs; i++) {
            const lit_t gate_input = gate->inputs[i];
            const var_t gate_var = lit_to_var(gate_input);
            node_shared* occ_node = circuit->nodes[gate_var];
            bool has_connection = false;
            var_t connection = 0;
            
            for (size_t var_id = bit_vector_init_iteration(occ_node->influences); bit_vector_iterate(occ_node->influences); var_id = bit_vector_next(occ_node->influences)) {
                Var* var = vector_get(circuit->vars, var_id);
                
                if (var->scope->scope_id < scope->scope_id) {
                    continue;
                }
                
                // Check whether this variable connects some variable sets
                while (partitions[partitions[var_id]] != partitions[var_id]) { // compactify
                    partitions[var_id] = partitions[partitions[var_id]];
                }
                if (!has_connection) {
                    has_connection = true;
                    connection = partitions[var_id];
                    continue;
                }
                assert(has_connection);
                if (connection < partitions[var_id]) {
                    partitions[partitions[var_id]] = connection;
                    partitions[var_id] = connection;
                }
                if (connection > partitions[var_id]) {
                    partitions[connection] = partitions[var_id];
                    connection = partitions[var_id];
                }
            }
        }
        
        /*for (size_t i = 0; i < circuit->num_vars; i++) {
            printf("%zu %d\n", i, partitions[i]);
        }*/
    
        // This loop does two things at the same time. (that are joined into a single pass over the variables)
        // 1. It flattens the clausegraph to ensure that all variables of the same variable group have the same number
        // 2. It maps all variables to their group number (continuously numbered from 0 to maximal group number)
        // The trick is that the number of groups is smaller or equal to the number i of variables processed so far.
        // This allows us to use the entries in cg for indices <i as group numbers instead of variable numbers.
        size_t num_groups = 0;
        map* groups = map_init();
        int_vector* pivots = int_vector_init();
        for (size_t i = 0; i < vector_count(circuit->vars); i++) {
            Var* var = vector_get(circuit->vars, i);
            const var_t var_id = var->var_id;
            assert(var != NULL);
            assert(var_id < circuit->num_vars);
            if (var->removed) {
                continue;
            }
            if (var->scope->scope_id < scope->scope_id) {
                continue;
            }
            if (var->scope != scope) {
                continue;
            }
            
            //logging_debug("Compactifying clause graph for var %d\n", var->shared.id);
            do { // compactify the entry
                partitions[var_id] = partitions[partitions[var_id]];
            } while (partitions[var_id] >= var_id && partitions[var_id] != partitions[partitions[var_id]]);
            
            if (partitions[var_id] == var_id) {
                num_groups++;
                ScopeNode* scope_node = circuit_new_scope_node(circuit, scope->qtype, circuit->max_num + 1);
                map_add(groups, var_id, scope_node);
                move_variable(var, scope, scope_node->scope);
                link_scope(scope, scope_node->scope);
                int_vector_add(pivots, var_id);
            } else {
                assert(partitions[var_id] < var_id);
                assert(num_groups > 0);
                
                ScopeNode* scope_node = map_get(groups, partitions[var_id]);
                move_variable(var, scope, scope_node->scope);
                link_scope(scope, scope_node->scope);
            }
            logging_debug("Variable %d is in variable group %d\n", var_id, partitions[var_id]);
        }
        
        logging_info("detected %zu groups\n", num_groups);
        
        //int_vector_print(pivots);
        
        // make a copy of original gate inputs since the gate is modified
        int_vector* original_gate_inputs = int_vector_init();
        for (size_t i = 0; i < gate->num_inputs; i++) {
            lit_t gate_input = gate->inputs[i];
            int_vector_add(original_gate_inputs, gate_input);
        }
        
        // connect newly created scope_nodes to circuit
        for (size_t i = 0; i < int_vector_count(pivots); i++) {
            var_t pivot = int_vector_get(pivots, i);
            ScopeNode* scope_node = map_get(groups, pivot);
            int_vector* relevant_gate_inputs = int_vector_init();
            for (size_t j = 0; j < int_vector_count(original_gate_inputs); j++) {
                lit_t gate_input = int_vector_get(original_gate_inputs, j);
                node_shared* gate_node = circuit->nodes[lit_to_var(gate_input)];
                for (size_t k = 0; k < vector_count(scope_node->scope->vars); k++) {
                    Var* var = vector_get(scope_node->scope->vars, k);
                    if (bit_vector_contains(gate_node->influences, var->var_id)) {
                        int_vector_add(relevant_gate_inputs, gate_input);
                        break;
                    }
                }
            }
            
            //int_vector_print(relevant_gate_inputs);
            
            assert(int_vector_count(relevant_gate_inputs) > 0);
            if (int_vector_count(relevant_gate_inputs) > 1) {
                // have to create a new gate containing with the gate_inputs
                Gate* new_gate = circuit_add_gate(circuit, circuit->max_num + 1, gate->type);
                new_gate->shared.num_occ = 1;
                new_gate->shared.influences = bit_vector_init(0, circuit->max_num);
                for (size_t j = 0; j < int_vector_count(relevant_gate_inputs); j++) {
                    lit_t gate_input = int_vector_get(relevant_gate_inputs, j);
                    remove_input_from_gate(gate, gate_input);
                    circuit_add_to_gate(circuit, new_gate, gate_input);
                    
                    node_shared* gate_node = circuit->nodes[lit_to_var(gate_input)];
                    bit_vector_update_or(new_gate->shared.influences, gate_node->influences);
                }
                scope_node->sub = create_lit(new_gate->shared.id, false);
                circuit_add_to_gate(circuit, gate, create_lit(scope_node->shared.id, false));
            } else {
                // we do not have to create a new gate
                assert(int_vector_count(relevant_gate_inputs) == 1);
                lit_t gate_input = int_vector_get(relevant_gate_inputs, 0);
                remove_input_from_gate(gate, gate_input);
                scope_node->sub = gate_input;
                circuit_add_to_gate(circuit, gate, create_lit(scope_node->shared.id, false));
            }
            
            // fix assumptions
            assert(scope_node->shared.num_occ == 0);
            scope_node->shared.num_occ = 1;
            scope_node->shared.influences = bit_vector_init(0, circuit->max_num);
            node_shared* sub_node = circuit->nodes[lit_to_var(scope_node->sub)];
            bit_vector_update_or(scope_node->shared.influences, sub_node->influences);
            
            int_vector_free(relevant_gate_inputs);
        }
        
        // Remove all next pointer from scope *before* freeing
        // The next pointer are created for the new scope nodes during reencoding
        while (scope->num_next > 0) {
            Scope* next = scope->next[0];
            unlink_scope(scope, next);
        }
        
        if (scope != circuit->top_level) {
            remove_scope(circuit, scope);
        }
        
        int_vector_free(original_gate_inputs);
        
        //circuit_print_qcir(circuit);
        
        //debug printouts
        /*logging_info("Detected %zu independent variable groups.\n", num_groups);
         if (LOGGING_DEBUG) {
         for (size_t i = 0; i < vector_count(scope->groups); i++) {
         logging_debug("group %zu: ", i);
         int_vector* group = vector_get(scope->groups, i);
         assert(int_vector_is_sorted(group));
         for (size_t j = 0; j < int_vector_count(group); j++) {
         int var_id = int_vector_get(group, j);
         logging_debug("%d ", var_id);
         }
         logging_debug("\n");
         }
         }*/
    }
}

static void unprenex_by_miniscoping_recursive(Circuit*, Scope*, var_t*);
static void unprenex_by_miniscoping_recursive(Circuit* circuit, Scope* scope, var_t* partitions) {
    const size_t num_next = scope->num_next;
    // store original number of next scopes since in the recursive call,
    // scopes may be splitted and added to the end of scope->next, and we
    // do not need to recurse into these nodes
    // Note: In order for this to work, scopes must not be deleted
    for (size_t i = 0; i < num_next; i++) {
        unprenex_by_miniscoping_recursive(circuit, scope->next[i], partitions);
    }
    
    if (scope == circuit->top_level && vector_count(scope->vars) == 0) {
        return;
    }
    assert(vector_count(scope->vars) > 0);
    
    if (scope->node) {
        ScopeNode* node = circuit->nodes[scope->node];
        apply_miniscoping(circuit, scope, lit_to_var(node->sub), partitions);
    } else {
        apply_miniscoping(circuit, scope, lit_to_var(circuit->output), partitions);
    }
}


/**
 * Converts the circuit into non-prenex form by applying mini-scoping rules.
 *
 * Implementation: Descend to leaf scopes, apply mini-scoping rules to them,
 * i.e., they may be split, then traverse the quantifier tree upwards.
 */
void circuit_unprenex_by_miniscoping(Circuit* circuit) {
    var_t* partitions = malloc(circuit->num_vars * sizeof(var_t));
    for (size_t i = 0; i < circuit->num_vars; i++) {
        partitions[i] = i;
    }
    //circuit_compute_variable_influence(circuit);
    unprenex_by_miniscoping_recursive(circuit, circuit->top_level, partitions);
    free(partitions);
    //circuit_print_qcir(circuit);
    //circuit_check_occurrences(circuit);
    circuit_reencode(circuit);
}

void circuit_compute_scope_influence(Circuit* circuit) {
    api_expect(circuit->phase == ENCODED, "circuit must be encoded\n");
    const size_t max_depth = circuit->max_depth;
    
    // Compute which scope influences which gate
    for (size_t i = 1; i <= circuit->max_num; i++) {
        node_shared* node = circuit->nodes[i];
        assert(node != NULL);
        bit_vector_free(node->influences);
        node->influences = bit_vector_init(0, max_depth);
        
        const node_type type = circuit->types[i];
        
        if (type == NODE_VAR) {
            Var* var = circuit->nodes[i];
            assert(var->scope->depth < max_depth);
            bit_vector_add(node->influences, var->scope->depth);
        } else if (type == NODE_GATE) {
            Gate* gate = circuit->nodes[i];
            for (size_t j = 0; j < gate->num_inputs; j++) {
                const var_t var = lit_to_var(gate->inputs[j]);
                node_shared* occ_node = circuit->nodes[var];
                assert(occ_node->influences != NULL);
                bit_vector_update_or(node->influences, occ_node->influences);
                
                if (circuit->types[var] == NODE_SCOPE) {
                    // Do not propagate scope over scope_node
                    //ScopeNode* scope_node = circuit->nodes[var];
                    //bit_vector_remove(node->influences, scope_node->scope->depth);
                }
            }
        } else {
            assert(type == NODE_SCOPE);
            ScopeNode* scope_node = circuit->nodes[i];
            const var_t var = lit_to_var(scope_node->sub);
            node_shared* occ_node = circuit->nodes[var];
            assert(occ_node->influences != NULL);
            bit_vector_update_or(node->influences, occ_node->influences);
            
            if (circuit->types[var] == NODE_SCOPE) {
                // Do not propagate scope over scope_node
                //ScopeNode* other_scope_node = circuit->nodes[var];
                //bit_vector_remove(node->influences, other_scope_node->scope->depth);
            }
            // Special case: scope node is directly below top level scope
            assert(scope_node->scope->prev != NULL);
            if (scope_node->scope->prev == circuit->top_level) {
                bit_vector_add(node->influences, circuit->top_level->depth);
            }
        }
        
        if (bit_vector_min(node->influences) == BIT_VECTOR_NO_ENTRY) {
            bit_vector_add(node->influences, circuit->top_level->depth);
        }
    }
}

static void compute_relevant_scopes_recursively(Circuit* circuit, bit_vector* relevant, lit_t node_lit) {
    const var_t node_var = lit_to_var(node_lit);
    const node_type type = circuit->types[node_var];
    const node_shared* node = circuit->nodes[node_var];
    
    if (bit_vector_equal(node->relevant_for, relevant)) {
        // only propagate new information
        return;
    }
    
    bit_vector_update_or(node->relevant_for, relevant);
    
    if (type == NODE_SCOPE) {
        ScopeNode* scope_node = (ScopeNode*)node;
        bit_vector_add(relevant, scope_node->scope->scope_id);
        bit_vector_add(node->relevant_for, scope_node->scope->scope_id);
        compute_relevant_scopes_recursively(circuit, relevant, scope_node->sub);
        bit_vector_remove(relevant, scope_node->scope->scope_id);
    } else if (type == NODE_GATE) {
        Gate* gate = (Gate*)node;
        for (size_t i = 0; i < gate->num_inputs; i++) {
            compute_relevant_scopes_recursively(circuit, relevant, gate->inputs[i]);
        }
    }
}

void circuit_compute_relevant_scopes(Circuit* circuit) {
    bit_vector* relevant = bit_vector_init(0, circuit->max_scope_id);
    for (Scope* scope = circuit->top_level; scope != NULL; scope = circuit_next_scope_in_prefix(scope)) {
        bit_vector_add(relevant, scope->scope_id);
    }
    
    for (size_t i = 1; i <= circuit->max_num; i++) {
        node_shared* node = circuit->nodes[i];
        bit_vector_free(node->relevant_for);
        node->relevant_for = bit_vector_init(0, circuit->max_scope_id);
    }
    
    compute_relevant_scopes_recursively(circuit, relevant, circuit->output);
    bit_vector_free(relevant);
    
    /*for (size_t i = 1; i <= circuit->max_num; i++) {
        node_shared* node = circuit->nodes[i];
        assert(node != NULL);
        
        const node_type type = circuit->types[i];
        
        if (type == NODE_VAR) {
            continue;
        } else if (type == NODE_GATE) {
            Gate* gate = circuit->nodes[i];
            for (size_t j = 0; j < gate->num_inputs; j++) {
                const var_t var = lit_to_var(gate->inputs[j]);
                if (circuit->types[var] == NODE_VAR) {
                    continue;
                }
                node_shared* occ_node = circuit->nodes[var];
                assert(occ_node->relevant_for != NULL);
                bit_vector_update_or(node->relevant_for, occ_node->relevant_for);
            }
        } else {
            assert(type == NODE_SCOPE);
            ScopeNode* scope_node = circuit->nodes[i];
            const var_t var = lit_to_var(scope_node->sub);
            node_shared* occ_node = circuit->nodes[var];
            assert(occ_node->relevant_for != NULL);
            bit_vector_update_or(node->relevant_for, occ_node->relevant_for);
            
            // Special case: scope node is directly below top level scope
            assert(scope_node->scope->prev != NULL);
            if (scope_node->scope->prev == circuit->top_level) {
                bit_vector_add(node->relevant_for, circuit->top_level->scope_id);
            }
        }
        
        if (bit_vector_min(node->relevant_for) == BIT_VECTOR_NO_ENTRY) {
            bit_vector_add(node->relevant_for, circuit->top_level->scope_id);
        }
    }*/
}

bool circuit_is_2qbf(Circuit* circuit) {
    Scope* top_level = circuit->top_level;
    if (vector_count(top_level->vars) != 0) {
        return false;
    }
    
    if (circuit->top_level->num_next == 0) {
        return false;
    }
    
    assert(false);
    
    return false;
    /*Scope* universal = top_level->next;
    if (universal == NULL) {
        return false;
    }
    assert(universal->qtype == QUANT_FORALL);
    
    Scope* existential = universal->next;
    if (existential == NULL) {
        return false;
    }
    assert(existential->qtype == QUANT_EXISTS);
    assert(vector_count(existential->vars) > 0);
    
    return existential->next == NULL;*/
}


bool circuit_is_prenex(Circuit* circuit) {
    for (Scope* scope = circuit->top_level; scope != NULL; scope = scope->next[0]) {
        if (scope->num_next > 1) {
            return false;
        }
        if (scope->node != 0) {
            return false;
        }
        if (scope->num_next == 0) {
            return true;
        }
    }
    return true;
}

// Iterators

Scope* circuit_next_scope_in_prefix(Scope* scope) {
    if (scope->num_next == 0) {
        assert(scope->next == NULL);
        return NULL;
    }
    Scope* next = scope->next[0];
    if (next->node != 0) {
        return NULL;
    }
    return next;
}

