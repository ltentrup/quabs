//
//  qcir2qcir.c
//  caqe-qcir
//
//  Created by Leander Tentrup on 25.11.15.
//  Copyright (c) 2015 Saarland University. All rights reserved.
//

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#include "circuit.h"
#include "qcir.h"
#include "logging.h"
#include "getopt.h"

#define TMPFILE_PREFIX "/tmp/qcirdd"
#define FAIL_TRIES 100

void print_usage(const char* name) {
    printf("usage: %s [-h] <src> <dst> <run>\n\n"
           "optional arguments:\n"
           "  -h, --help\t\tshow this help message and exit\n", name);
}

int main(int argc, char * const argv[]) {
    const char *source_file = NULL;
    const char *destination_file = NULL;
    const char *executable = NULL;
    
    // Handling of command line arguments
    const char * ch;
    while ((ch = GETOPT(argc, argv)) != NULL) {
        GETOPT_SWITCH(ch) {
            GETOPT_OPT("-h"):
            print_usage(argv[0]);
            return 1;
            break;
        GETOPT_MISSING_ARG:
            printf("missing argument to %s\n", ch);
            /* FALLTHROUGH */
        GETOPT_DEFAULT:
            printf("unknown argument %s\n", ch);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    if (optind + 3 > argc) {
        logging_error("Too few arguments, see --help for more information\n");
        return 1;
    }
    
    // Initialize the random number generator
    time_t t;
    srand((unsigned) time(&t));
    
    source_file = argv[optind];
    destination_file = argv[optind + 1];
    executable = argv[optind + 2];
    
    Circuit* circuit = circuit_init();
    if (!circuit) {
        return 1;
    }
    
    int error = circuit_open_and_read_qcir_file(circuit, source_file, false);
    if (error) {
        return 1;
    }
    
    // get initial exit status
    char* initial_cmd = malloc(sizeof(char) * (strlen(executable) + strlen(source_file) + 20));
    sprintf(initial_cmd, "%s %s &>/dev/null", executable, source_file);
    printf("Executing \"%s\"\n", initial_cmd);
    
    int exit_code = system(initial_cmd);
    exit_code = WEXITSTATUS(exit_code);
    free(initial_cmd);
    //printf("exit code %d\n", exit_code);
    
    circuit_open_and_write_qcir_file(circuit, destination_file);
    
    // We will work on a temp file such that dest always contains an instance where the error occurred
    char* tmp_file = malloc(sizeof(char) * (strlen(TMPFILE_PREFIX) + 6));
    sprintf(tmp_file, "%s%d", TMPFILE_PREFIX, rand() % 100000);
    
    int failed = 0;
    while (failed < FAIL_TRIES) {
        // Modify circuit by removing a gate
        var_t remove = rand() % circuit->max_num;
        if (circuit->types[remove] != NODE_GATE || remove == lit_to_var(circuit->output)) {
            failed++;
            continue;
        }
        printf("Try to remove node %d...", remove);
        fflush(stdout);
        
        // copy quantifier prefix
        Circuit* modified = circuit_init();
        for (Scope* scope = circuit->top_level; scope != NULL; scope = circuit_next_scope_in_prefix(scope)) {
            Scope* copy = circuit_init_scope(modified, scope->qtype);
            for (size_t i = 0; i < vector_count(scope->vars); i++) {
                Var* var = vector_get(scope->vars, i);
                circuit_new_var(modified, copy, var->shared.orig_id);
            }
        }
        
        // set output
        circuit_set_output(modified, circuit->output);
        
        // copy circuit
        for (size_t i = 1; i <= circuit->max_num; i++) {
            if (i == remove) {
                continue;
            }
            
            const node_type type = circuit->types[i];
            if (type == NODE_VAR || type == 0) {
                continue;
            } else if (type == NODE_GATE) {
                Gate* gate = circuit->nodes[i];
                Gate* copy = circuit_add_gate(modified, i, gate->type);
                for (size_t j = 0; j < gate->num_inputs; j++) {
                    const lit_t input = gate->inputs[j];
                    if (lit_to_var(input) == remove) {
                        continue;
                    }
                    circuit_add_to_gate(modified, copy, input);
                }
            } else if (type == NODE_SCOPE) {
                ScopeNode* scope_node = circuit->nodes[i];
                ScopeNode* copy = circuit_new_scope_node(circuit, scope_node->scope->qtype, i);
                // copy variables
                for (size_t j = 0; j < vector_count(scope_node->scope->vars); j++) {
                    Var* var = vector_get(scope_node->scope->vars, j);
                    circuit_new_var(modified, copy->scope, var->shared.orig_id);
                }
                if (lit_to_var(scope_node->sub) == remove && modified->nodes[remove] == NULL) {
                    circuit_add_gate(circuit, remove, GATE_AND);
                }
                circuit_set_scope_node(circuit, copy, scope_node->sub);
            } else {
                assert(false);
            }
        }
        
        circuit_open_and_write_qcir_file(modified, tmp_file);
        
        char* modified_cmd = malloc(sizeof(char) * (strlen(executable) + strlen(tmp_file) + 20));
        sprintf(modified_cmd, "%s %s &>/dev/null", executable, tmp_file);
        //printf("Executing \"%s\"\n", modified_cmd);
        
        int modified_exit_code = system(modified_cmd);
        modified_exit_code = WEXITSTATUS(modified_exit_code);
        free(modified_cmd);
        //printf("exit code %d\n", modified_exit_code);
        
        if (modified_exit_code == exit_code) {
            // successfull removal, still the same exit code
            circuit_open_and_write_qcir_file(modified, destination_file);
            circuit_free(circuit);
            circuit = modified;
            failed = 0;
            printf("successfull\n");
        } else {
            failed++;
            printf("unsuccessfull\n");
        }
    }
    
    circuit_free(circuit);
    
    unlink(tmp_file);
    free(tmp_file);
    
    return 0;
}
