//
//  circuit_print.c
//  caqe-qcir
//
//  Created by Leander Tentrup on 04.02.16.
//  Copyright © 2016 Saarland University. All rights reserved.
//

#include "circuit_print.h"

static void print_qcir(Circuit* circuit, FILE* file, bool symbol_table) {
    // Header
    //fprintf(file, "# Created with CAQE\n");
    fprintf(file, "#QCIR-G14 %zu\n", circuit->max_num);
    
    // quantifier prefix
    Scope* top_level = circuit->top_level;
    assert(top_level != NULL);
    for (Scope* scope = top_level; scope != NULL; scope = circuit_next_scope_in_prefix(scope)) {
        if (vector_count(scope->vars) == 0) {
            continue;
        }
        if (scope->qtype == QUANT_EXISTS) {
            fprintf(file, "exists(");
        } else {
            assert(scope->qtype == QUANT_FORALL);
            fprintf(file, "forall(");
        }
        
        for (size_t i = 0; i < vector_count(scope->vars); i++) {
            Var* var = vector_get(scope->vars, i);
            fprintf(file, "%d", var->shared.id);
            if (i + 1 < vector_count(scope->vars)) {
                fprintf(file, ", ");
            }
        }
        
        fprintf(file, ")\n");
    }
    
    // Output
    fprintf(file, "output(%d)\n", circuit->output);
    
    // Circuit
    for (size_t i = 0; i <= circuit->max_num; i++) {
        if (circuit->types[i] == NODE_GATE) {
            Gate* gate = circuit->nodes[i];
            fprintf(file, "%d = ", gate->shared.id);
            switch (gate->type) {
                case GATE_AND:
                    fprintf(file, "and(");
                    break;
                case GATE_OR:
                    fprintf(file, "or(");
                    break;
                default:
                    assert(false);
                    break;
            }
            for (size_t j = 0; j < gate->num_inputs; j++) {
                lit_t gate_input = gate->inputs[j];
                fprintf(file, "%d", gate_input);
                if (j + 1 < gate->num_inputs) {
                    fprintf(file, ", ");
                }
            }
            fprintf(file, ")\n");
        } else if (circuit->types[i] == NODE_SCOPE) {
            ScopeNode* node = circuit->nodes[i];
            fprintf(file, "%d = ", node->shared.id);
            switch (node->scope->qtype) {
                case QUANT_FORALL:
                    fprintf(file, "forall(");
                    break;
                case QUANT_EXISTS:
                    fprintf(file, "exists(");
                    break;
                default:
                    assert(false);
                    break;
            }
            for (size_t j = 0; j < vector_count(node->scope->vars); j++) {
                Var* var = vector_get(node->scope->vars, j);
                fprintf(file, "%d", var->shared.id);
                if (j + 1 < vector_count(node->scope->vars)) {
                    fprintf(file, ", ");
                }
            }
            fprintf(file, "; %d)\n", node->sub);
        }
    }
    
    if (symbol_table) {
        // Print symboltable for variables
        fprintf(file, "#symboltable\n");
        for (size_t i = 0; i < vector_count(circuit->vars); i++) {
            Var* var = vector_get(circuit->vars, i);
            fprintf(file, "#%d %d\n", var->shared.id, var->shared.orig_id);
        }
        fprintf(file, "\n\n");
    }
}

void circuit_print_qcir(Circuit* circuit) {
    print_qcir(circuit, stdout, false);
}

void circuit_write_qcir(Circuit* circuit, FILE* file) {
    print_qcir(circuit, file, false);
}

void circuit_write_qcir_with_symboltable(Circuit* circuit, FILE* file) {
    print_qcir(circuit, file, true);
}

void circuit_print_dot(Circuit* circuit) {
    // DOT header
    //printf("// Created with CAQE\n");
    printf("digraph circuit {\n");
    
    // Scopes
    for (Scope* scope = circuit->top_level; scope != NULL; scope = circuit_next_scope_in_prefix(scope)) {
        if (vector_count(scope->vars) == 0) {
            continue;
        }
        
        for (size_t i = 0; i < vector_count(scope->vars); i++) {
            Var* var = vector_get(scope->vars, i);
            printf("\tn%d[label=\"%d\",color=\"%s\"];\n", var->shared.id, var->shared.id, scope->qtype == QUANT_FORALL ? "red" : "green");
        }
    }
    
    // Output
    printf("\tn%d[label=\"%d\",color=\"blue\"];\n", circuit->output, circuit->output);
    
    // Circuit
    for (size_t i = 0; i <= circuit->max_num; i++) {
        if (circuit->types[i] == NODE_GATE) {
            Gate* gate = circuit->nodes[i];
            switch (gate->type) {
                case GATE_AND:
                    printf("\tn%d[label=\"AND\n(%d)\"];\n", gate->shared.id, gate->shared.id);
                    break;
                case GATE_OR:
                    printf("\tn%d[label=\"OR\n(%d)\"];\n", gate->shared.id, gate->shared.id);
                    break;
                default:
                    assert(false);
                    break;
            }
            for (size_t j = 0; j < gate->num_inputs; j++) {
                lit_t gate_input = gate->inputs[j];
                var_t input_var = lit_to_var(gate_input);
                printf("\tn%d -> n%d[arrowhead=\"%s\"];\n", gate->shared.id, input_var, gate_input > 0 ? "none" : "dot");
            }

        } else if (circuit->types[i] == NODE_SCOPE) {
            ScopeNode* node = circuit->nodes[i];
            switch (node->scope->qtype) {
                case QUANT_FORALL:
                    printf("\tn%d[label=\"forall\n(%d)\"];\n", node->shared.id, node->shared.id);
                    break;
                case QUANT_EXISTS:
                    printf("\tn%d[label=\"exists\n(%d)\"];\n", node->shared.id, node->shared.id);
                    break;
                default:
                    assert(false);
                    break;
            }
            for (size_t j = 0; j < vector_count(node->scope->vars); j++) {
                Var* var = vector_get(node->scope->vars, j);
                printf("\tn%d[label=\"%d\",color=\"%s\"];\n", var->shared.id, var->shared.id, node->scope->qtype == QUANT_FORALL ? "red" : "green");
                printf("\tn%d -> n%d[style=dashed,arrowhead=none];\n", node->shared.id, var->shared.id);
            }
            printf("\tn%d -> n%d[arrowhead=\"%s\"];\n", node->shared.id, lit_to_var(node->sub), node->sub > 0 ? "none" : "dot");
        }
    }
    
    
    // DOT footer
    printf("}\n");
}

void circuit_print_qdimacs(Circuit* circuit) {
    // Header
    //printf("c Created with CAQE\n");
    
    size_t num_clauses = 1;  // circuit output
    for (size_t i = 1; i <= circuit->max_num; i++) {
        assert(circuit->nodes[i] != NULL);
        if (circuit->types[i] != NODE_GATE) {
            continue;
        }
        Gate* gate = circuit->nodes[i];
        num_clauses += gate->num_inputs + 1;
    }
    
    printf("p cnf %zu %lu\n", circuit->max_num, num_clauses);
    
    // Scopes
    for (Scope* scope = circuit->top_level; scope != NULL; scope = circuit_next_scope_in_prefix(scope)) {
        if (vector_count(scope->vars) == 0) {
            continue;
        }
        if (scope->qtype == QUANT_EXISTS) {
            printf("e ");
        } else {
            assert(scope->qtype == QUANT_FORALL);
            printf("a ");
        }
        
        for (size_t i = 0; i < vector_count(scope->vars); i++) {
            Var* var = vector_get(scope->vars, i);
            printf("%d ", var->shared.id);
        }
        
        printf("0\n");
    }
    // Last scope contains Tseitin variables
    printf("e ");
    for (size_t i = 1; i <= circuit->max_num; i++) {
        if (circuit->types[i] != NODE_GATE) {
            continue;
        }
        printf("%zu ", i);
    }
    printf("0\n");
    
    int_vector* clause = int_vector_init();
    for (size_t i = 1; i <= circuit->max_num; i++) {
        assert(circuit->nodes[i] != NULL);
        if (circuit->types[i] != NODE_GATE) {
            continue;
        }
        Gate* gate = circuit->nodes[i];
        gate_type gate_type = gate->type;
        
        // Build CNF using Tseitin transformation
        int_vector_reset(clause);
        for (size_t j = 0; j < gate->num_inputs; j++) {
            lit_t gate_input = gate->inputs[j];
            if (gate_type == GATE_OR) {
                printf("%d ", -gate_input);
                printf("%d ", gate->shared.id);
                int_vector_add(clause, gate_input);
            } else {
                assert(gate_type == GATE_AND);
                printf("%d ", gate_input);
                printf("%d ", -gate->shared.id);
                int_vector_add(clause, -gate_input);
            }
            printf("0\n");
        }
        
        for (size_t j = 0; j < int_vector_count(clause); j++) {
            int lit = int_vector_get(clause, j);
            printf("%d ", lit);
        }
        if (gate_type == GATE_OR) {
            printf("%d ", -gate->shared.id);
        } else {
            printf("%d ", gate->shared.id);
        }
        printf("0\n");
    }
    int_vector_free(clause);
    
    // Fix output value
    printf("%d ", circuit->output);  // TODO: output negated for universal?
    printf("0\n\n");
}
