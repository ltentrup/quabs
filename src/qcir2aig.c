//
//  qcir2aig.c
//  qcir2aig
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

#define CHAR_BUFFER_SIZE 100

static void print_usage(const char* name) {
    printf("usage: %s [-h] file\n\n"
           "Translates QBF instances in QCIR format to QAIGER.\n\n"
           "optional arguments:\n"
           "  --aig\t\toutput in binary QAIGER format\n"
           "  -h, --help\tshow this help message and exit\n", name);
}

typedef struct {
    Circuit* circuit;
    aiger* target;
    unsigned next_aiger_literal;
    map* lit2lit;
    int_queue gate;
} qcir2aig;

static unsigned new_aiger_lit(qcir2aig* data) {
    unsigned next = data->next_aiger_literal++;
    return next * 2;
}

static void import_variables(qcir2aig* data) {
    char encoding[CHAR_BUFFER_SIZE];
    int level = (data->circuit->top_level->qtype == QUANT_EXISTS) ? 0 : 1;
    for (Scope* scope = data->circuit->top_level; scope != NULL; scope = circuit_next_scope_in_prefix(scope)) {
        for (size_t j = 0; j < vector_count(scope->vars); j++) {
            Var* var = vector_get(scope->vars, j);
            snprintf(encoding, CHAR_BUFFER_SIZE, "%d %d", level, var->shared.orig_id);
            unsigned aiger_lit = new_aiger_lit(data);
            aiger_add_input(data->target, aiger_lit, encoding);
            map_add(data->lit2lit, var->shared.id, (void*)(uintptr_t)aiger_lit);
        }
        level++;
    }
}

static void import_circuit(qcir2aig* data) {
    for (size_t i = 1; i <= data->circuit->max_num; i++) {
        if (data->circuit->types[i] != NODE_GATE) {
            assert(data->circuit->types[i] == NODE_VAR);
            continue;
        }
        
        assert(int_queue_is_empty(&data->gate));
        
        Gate* gate = data->circuit->nodes[i];
        for (size_t j = 0; j < gate->num_inputs; j++) {
            lit_t input = gate->inputs[j];
            var_t input_var = lit_to_var(input);
            unsigned translated_input = (uintptr_t)map_get(data->lit2lit, input_var);
            if (input < 0) {
                translated_input = aiger_not(translated_input);
            }
            int_queue_push(&data->gate, translated_input);
        }
        unsigned result;
        if (gate->type == GATE_OR) {
            result = encode_or(data->target, &data->next_aiger_literal, &data->gate);
        } else {
            assert(gate->type == GATE_AND);
            result = encode_and(data->target, &data->next_aiger_literal, &data->gate);
        }
        map_add(data->lit2lit, gate->shared.id, (void*)(uintptr_t)result);
    }
    
    // add output
    unsigned output = (uintptr_t)map_get(data->lit2lit, data->circuit->output);
    aiger_add_output(data->target, output, NULL);
}


int main(int argc, char * const argv[]) {
    const char *file_name = NULL;
    FILE* file = NULL;
    aiger_mode mode = aiger_ascii_mode;
    
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
                } else if (strncmp(argv[current_pos], "--aig", 5) == 0) {
                    // print in ascii aiger format
                    mode = aiger_binary_mode;
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
    
    qcir2aig data;
    data.circuit = NULL;
    data.target = aiger_init();
    data.next_aiger_literal = 1;
    data.lit2lit = map_init();
    int_queue_init(&data.gate);
    
    data.circuit = circuit_init();
    if (!data.circuit) {
        return 1;
    }
    
    int error = 0;
    if (file == NULL) {
        error = circuit_open_and_read_qcir_file(data.circuit, file_name, false);
    } else {
        error = circuit_from_qcir(data.circuit, file, false);
    }
    if (error) {
        return 1;
    }
    
    if (!circuit_is_prenex(data.circuit)) {
        logging_error("QCIR is non-prenex, hence, cannot be transformed to QAIGER\n");
        return 1;
    }
    
    circuit_reencode(data.circuit);
    
    //circuit_print_qcir(data.circuit);
    
    import_variables(&data);
    import_circuit(&data);
    
    aiger_reencode(data.target);

    aiger_add_comment(data.target, "Transformed to QAIGER file format (https://github.com/ltentrup/QAIGER)");
    aiger_add_comment(data.target, "using qcir2qaiger (https://github.com/ltentrup/quabs).");

    aiger_write_to_file(data.target, mode, stdout);
    
    
    circuit_free(data.circuit);
    
    return 0;
}
