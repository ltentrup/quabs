//
//  circuit.h
//  caqe-qcir
//
//  Created by Leander Tentrup on 01.07.15.
//  Copyright (c) 2015 Saarland University. All rights reserved.
//

#ifndef __caqe_qcir__circuit__
#define __caqe_qcir__circuit__

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>

#include "vector.h"
#include "bit_vector.h"

typedef int32_t lit_t;
typedef uint32_t var_t;

typedef enum {
    NODE_VAR = 1, NODE_GATE = 2, NODE_SCOPE = 3
} node_type;

typedef enum {
    GATE_AND = 0, GATE_OR
} gate_type;

typedef enum {
    QUANT_EXISTS = 0, QUANT_FORALL, QUANT_FREE
} quantifier_type;

typedef enum {
    POLARITY_UNDEFINED = 0, POLARITY_POS, POLARITY_NEG, POLARITY_NONE
} polarity_type;

typedef enum {
    BUILDING,
    ENCODED,
    PROPAGATION
} circuit_phases;

typedef enum {
    CIRCUIT_NO_OPTIONS = 0,
    CIRCUIT_KEEP_VARIABLES = 1 << 0
} circuit_options;

typedef enum {
    QBF_RESULT_SAT     = 10,
    QBF_RESULT_UNSAT   = 20,
    QBF_RESULT_UNKNOWN = 30
} qbf_res;

typedef void node;
typedef struct node_shared node_shared;

typedef struct gate Gate;
typedef struct var Var;
typedef struct scope Scope;
typedef struct scope_node ScopeNode;
typedef struct circuit Circuit;
typedef struct propagation propagation;

/**
 * Contains all the shared attributes between the two node types (var and gate).
 * Must be the very first element in the var/gate structs.
 */
struct node_shared {
    var_t id;
    var_t orig_id;
    
    size_t num_occ;
    
    int value;        // (< 0, 0, > 0) = (false, undefined, true)
    
    bit_vector* influences;
    bit_vector* relevant_for;  // whether node is in scope of quantifier
    bool dfs_processed;
};


/**
 * The var struct contains the id of a variable, and the value used for evaluation.
 */
struct var {
    node_shared shared;
    
    var_t var_id;
    Scope* scope;
    bool removed;
    polarity_type polarity;
    quantifier_type orig_quant;
};


struct gate {
    node_shared shared;
    
    gate_type type;
    
    size_t size_inputs;
    size_t num_inputs;          // number of inputs
    lit_t* inputs;
    
    var_t min_node;   // minimal node in subtree of gate
    
    bool conflict;    // contains lit and -lit
    bool reachable;   // indicates that gate is reachable
    bool keep;        // do not remove during preprocessing
    lit_t negation;   // indicates node of negated gate (for NNF conversion)
    
    // miniscoping
    var_t owner;
};

struct scope {
    // scope_id is unique and strictly increasing with respect to quantifier level
    // after calling circuit_reencode, scope_id are consecutive
    var_t scope_id;
    var_t depth;
    var_t max_depth;
    quantifier_type qtype;
    Circuit* circuit;
    vector* vars;
    var_t node;
    size_t num_next;
    Scope** next;
    Scope* prev;
};

struct scope_node {
    node_shared shared;
    Scope* scope;
    lit_t sub;
    var_t min_node; // minimal node in subtree of gate
};

struct circuit {
    node_type* types;
    node** nodes;
    size_t max_num;
    size_t size;
    size_t num_vars;
    lit_t output;
    vector* vars;
    circuit_phases phase;
    
    // Scopes
    var_t current_scope_id;
    var_t max_scope_id;
    var_t current_depth;
    var_t max_depth;
    Scope* previous_scope;
    Scope* top_level;
    
    // Propagation
    propagation* queue;
};


// Helper
static inline var_t lit_to_var(lit_t lit) { return lit < 0 ? (var_t)-lit : (var_t)lit; }
static inline lit_t create_lit(var_t var, bool negated) { return negated ? -(lit_t)var : (lit_t)var; }
static inline lit_t create_lit_from_value(var_t var, int value) {
    assert(value != 0);
    return create_lit(var, value < 0);
}

static inline gate_type circuit_negate_gate_type(gate_type type) {
    switch (type) {
        case GATE_AND:
            return GATE_OR;
            
        case GATE_OR:
            return GATE_AND;
            
        default:
            abort();
    }
}

static inline quantifier_type circuit_negate_quantifier_type(quantifier_type qtype) {
    switch (qtype) {
        case QUANT_EXISTS:
            return QUANT_FORALL;
            
        case QUANT_FORALL:
            return QUANT_EXISTS;
            
        default:
            abort();
    }
}

static inline gate_type normalize_gate_type(gate_type type, quantifier_type qtype) {
    if (qtype == QUANT_FORALL) {
        type = circuit_negate_gate_type(type);
    }
    return type;
}


// Building
Circuit* circuit_init(void);
void circuit_free(Circuit*);
void circuit_adjust(Circuit*, size_t max_num);
void circuit_set_output(Circuit*, lit_t node_id);
Var* circuit_new_var(Circuit*, Scope*, var_t var_id);
Scope* circuit_init_scope(Circuit*, quantifier_type);
ScopeNode* circuit_new_scope_node(Circuit*, quantifier_type, var_t node_id);
void circuit_set_scope_node(Circuit*, ScopeNode*, lit_t);
Gate* circuit_add_gate(Circuit*, var_t gate_id, gate_type);
bool circuit_add_to_gate(Circuit*, Gate*, lit_t);

// Sanitize
void circuit_reencode(Circuit*);

// Propagation
void circuit_set_value(Circuit*, var_t node, int value);
int circuit_get_value(Circuit*, var_t node);

/**
 * Evaluates the circuit based on values set by @link circuit_set_value @/link.
 */
void circuit_evaluate(Circuit*);

/**
 * Evaluates the circuit with the restriction that circuit values whose absolute
 * value is greater than value are treated as undefined.
 * @seealso circuit_evaluate
 */
void circuit_evaluate_max(Circuit*, int value);

// Preprocessing
void circuit_preprocess(Circuit*);
void circuit_normalize_quantifier(Circuit*);
void circuit_to_prenex(Circuit*);
void circuit_unprenex_by_miniscoping(Circuit*);
void circuit_flatten_gates(Circuit*);

// Analysis
//void circuit_compute_variable_influence(Circuit*);
void circuit_compute_scope_influence(Circuit*);
void circuit_compute_relevant_scopes(Circuit*);
bool circuit_is_prenex(Circuit*);


// Iterators
Scope* circuit_next_scope_in_prefix(Scope*);


#endif /* defined(__caqe_qcir__circuit__) */
