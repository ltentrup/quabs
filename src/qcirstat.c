//
//  main.c
//  qcirstat
//
//  Created by Leander Tentrup on 31.01.17.
//  Copyright © 2017 Saarland University. All rights reserved.
//

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "circuit.h"
#include "circuit_print.h"
#include "qcir.h"
#include "logging.h"
#include "getopt.h"

static void print_usage(const char* name) {
    printf("usage: %s [-h] [input_file]\n\n"
           "optional arguments:\n"
           "  -h, --help\t\tshow this help message and exit\n", name);
}

static unsigned calculate_circuit_depth(Circuit*);

int main(int argc, char* const argv[]) {
    const char *file_name = NULL;
    FILE* file = NULL;
    
    // Handling of command line arguments
    int current_pos;
    for (current_pos = 1; current_pos < argc; current_pos++) {
        const char first = argv[current_pos][0];
        if (first != '-') {
            // done reading options
            break;
        }
        if (first == '\0') {
            // we got an empty string
            break;
        }
        
        const char second = argv[current_pos][1];
        switch (second) {
            case 'h':
                // help
                print_usage(argv[0]);
                return 0;
                
            case '-':
                // long options
                if (strncmp(argv[current_pos], "--help", 6) == 0) {
                    print_usage(argv[0]);
                    return 0;
                } else {
                    logging_error("unknown argument %s\n", argv[current_pos]);
                    print_usage(argv[0]);
                    return 1;
                }
                break;
                
            default:
                logging_error("unknown argument %s\n", argv[current_pos]);
                print_usage(argv[0]);
                return 1;
        }
        
    }
    
    if (current_pos + 1 < argc) {
        logging_error("too many arguments, see --help for more information\n");
        return 1;
    }
    
    if (current_pos < argc) {
        file_name = argv[current_pos];
    } else {
        file = stdin;
    }
    
    Circuit* circuit = circuit_init();
    if (!circuit) {
        return 1;
    }
    
    int error = 0;
    if (file == NULL) {
        error = circuit_open_and_read_qcir_file(circuit, file_name, false);
    } else {
        error = circuit_from_qcir(circuit, file, false);
    }
    if (error) {
        return 1;
    }
    
    circuit_reencode(circuit);
    
    size_t num_vars = 0;
    size_t num_quant = 0;
    size_t num_gates = 0;
    
    for (Scope* scope = circuit->top_level; scope != NULL; scope = circuit_next_scope_in_prefix(scope)) {
        if (vector_count(scope->vars) == 0) {
            assert(scope->scope_id == 1);
            continue;
        }
        num_quant++;
    }
    
    for (size_t i = 1; i <= circuit->max_num; i++) {
        node_shared* node = circuit->nodes[i];
        if (node == NULL) {
            continue;
        }
        
        switch (circuit->types[i]) {
            case NODE_VAR:
                num_vars++;
                break;
            case NODE_GATE:
                num_gates++;
                break;
            case NODE_SCOPE:
                num_quant++;
                break;
                
            default:
                break;
        }
    }
    
    printf("quantifier: %zu\n", num_quant);
    printf("variables: %zu\n", num_vars);
    printf("gates: %zu\n", num_gates);
    
    printf("circuit depth: %u\n", calculate_circuit_depth(circuit));
    
    circuit_free(circuit);
    
    return 0;
}

static unsigned calculate_circuit_depth_recursive(Circuit* circuit, lit_t lit, unsigned current_depth) {
    const var_t var = lit_to_var(lit);
    switch (circuit->types[var]) {
        case NODE_VAR:
            return current_depth + 1;
        
        case NODE_SCOPE:
        {
            ScopeNode* scope = circuit->nodes[var];
            return calculate_circuit_depth_recursive(circuit, scope->sub, current_depth + 1) + 1;
        }
            
        case NODE_GATE:
        {
            Gate* gate = circuit->nodes[var];
            unsigned max = current_depth + 1;
            for (size_t i = 0; i < gate->num_inputs; i++) {
                unsigned recursive = calculate_circuit_depth_recursive(circuit, gate->inputs[i], current_depth + 1);
                if (recursive > max) {
                    max = recursive;
                }
            }
            return max;
        }
            
            break;
            
        default:
            abort();
    }
    abort();
}

static unsigned calculate_circuit_depth(Circuit* circuit) {
    return calculate_circuit_depth_recursive(circuit, circuit->output, 0);
}
