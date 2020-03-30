//
//  aig2qcir.c
//  aig2qcir
//
//  Created by Leander Tentrup on 31.08.16.
//  Copyright © 2016 Saarland University. All rights reserved.
//

#include <stdio.h>
#include <string.h>

#include "aiger.h"
#include "aiger_helper.h"
#include "circuit.h"
#include "circuit_print.h"
#include "qcir.h"
#include "logging.h"
#include "map.h"

static void print_usage(const char* name) {
    printf("usage: %s [-h] file\n\n"
           "optional arguments:\n"
           "  -h, --help\t show this help message and exit\n", name);
}

typedef struct {
    aiger* source;
    Circuit* target;
} aig2qcir;

static void import_variables(aig2qcir* data) {
    vector* scopes = vector_init();
    for (size_t i = 0; i < data->source->num_inputs; i++) {
        aiger_symbol input = data->source->inputs[i];
        assert(input.name != NULL);
        int level;
        int result = sscanf(input.name, "%d ", &level);
        if (result != 1) {
            exit(1);
        }

        while (level >= vector_count(scopes)) {
            // level does not exist yet
            size_t next_scope = vector_count(scopes);
            Scope* scope;
            if (next_scope % 2 == 0) {
                // existential
                scope = circuit_init_scope(data->target, QUANT_EXISTS);
            } else {
                // universal
                scope = circuit_init_scope(data->target, QUANT_FORALL);
            }
            vector_add(scopes, scope);
        }

        Scope* scope = vector_get(scopes, level);

        circuit_new_var(data->target, scope, input.lit / 2);
    }
    vector_free(scopes);
}

static lit_t aiger_lit_to_circuit_lit(unsigned aiger_lit) {
    if (aiger_lit % 2 == 1) {
        return -(aiger_strip(aiger_lit) / 2);
    } else {
        return aiger_lit / 2;
    }
}

static void import_circuit(aig2qcir* data) {
    for (size_t i = 0; i < data->source->num_ands; i++) {
        aiger_and and = data->source->ands[i];
        if (and.rhs0 == 0 || and.rhs1 == 0) {
            circuit_add_gate(data->target, and.lhs / 2, GATE_OR);
            continue;
        }
        Gate* gate = circuit_add_gate(data->target, and.lhs / 2, GATE_AND);
        if (and.rhs0 != 1) {
            circuit_add_to_gate(data->target, gate, aiger_lit_to_circuit_lit(and.rhs0));
        }
        if (and.rhs1 != 1) {
            circuit_add_to_gate(data->target, gate, aiger_lit_to_circuit_lit(and.rhs1));
        }
    }
    circuit_set_output(data->target, aiger_lit_to_circuit_lit(data->source->outputs[0].lit));
}


int main(int argc, char * const argv[]) {
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
    
    aig2qcir data;
    data.source = aiger_init();
    data.target = circuit_init();
    
    const char* error;
    if (file == NULL) {
        error = aiger_open_and_read_from_file(data.source, file_name);
    } else {
        error = aiger_read_from_file(data.source, file);
    }
    if (error) {
        printf("%s\n", aiger_error(data.source));
        return 1;
    }
    
    //aiger_write_to_file(data.source, aiger_ascii_mode, stdout);
    
    aiger_reencode(data.source);
    
    import_variables(&data);
    import_circuit(&data);

    circuit_reencode(data.target);
    circuit_preprocess(data.target);
    
    circuit_print_qcir(data.target);
    
    return 0;
}
