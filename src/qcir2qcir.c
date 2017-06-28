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

#include "circuit.h"
#include "circuit_print.h"
#include "qcir.h"
#include "logging.h"
#include "getopt.h"

static void print_usage(const char* name) {
    printf("usage: %s [-h] [-p] [input_file] [output_file]\n\n"
           "optional arguments:\n"
           "  -h, --help\t\tshow this help message and exit\n"
           "  -p, --preprocess\tapply preprocessing during conversion\n"
           "  --flatten\t\tstrict alternating between AND and OR gates\n", name);
}

int main(int argc, char* const argv[]) {
    const char* input_file_name = NULL;
    const char* output_file_name = NULL;
    FILE* input = NULL;
    FILE* output = NULL;
    volatile bool apply_preprocessing = false;
    volatile bool miniscoping = false;
    volatile bool prenexing = false;
    volatile bool flatten = false;
    
    // Handling of command line arguments
    opterr = 0; // Disable error messages as we handle them ourself
    const char* ch;
    while ((ch = GETOPT(argc, argv)) != NULL) {
        GETOPT_SWITCH(ch) {
        GETOPT_OPT("-h"):
            print_usage(argv[0]);
            return 1;
            break;
        GETOPT_OPT("-p"):
        GETOPT_OPT("--preprocess"):
            apply_preprocessing = true;
            break;
        GETOPT_OPT("--miniscoping"):
            miniscoping = true;
            break;
        GETOPT_OPT("--prenexing"):
            prenexing = true;
            break;
        GETOPT_OPT("--flatten"):
            flatten = true;
            break;
        GETOPT_OPT("-v"):
            logging_set_verbosity(VERBOSITY_ALL);
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
    
    if (optind + 2 < argc) {
        logging_error("too many arguments, see --help for more information\n");
        return 1;
    }
    
    if (miniscoping && prenexing) {
        logging_error("illegal argument combinationm (--miniscoping and --prenexing)\n");
        return 1;
    }
    
    if (optind + 1 < argc) {
        // given input and output file
        input_file_name = argv[optind];
        output_file_name = argv[optind + 1];
    } else if (optind < argc) {
        // only input given, print to stdout
        input_file_name = argv[optind];
        output = stdout;
    } else {
        // read from stdin, print to stdout
        input = stdin;
        output = stdout;
    }
    
    Circuit* circuit = circuit_init();
    if (!circuit) {
        return 1;
    }
    
    int error = 0;
    if (input == NULL) {
        error = circuit_open_and_read_qcir_file(circuit, input_file_name, false);
    } else {
        error = circuit_from_qcir(circuit, input, false);
    }
    if (error) {
        return 1;
    }
    
    circuit_reencode(circuit);

    
    if (apply_preprocessing) {
        circuit_preprocess(circuit);
    }
    
    if (flatten) {
        circuit_flatten_gates(circuit);
    }
    
    assert(!(miniscoping && prenexing));
    
    if (miniscoping) {
        circuit_unprenex_by_miniscoping(circuit);
    } else if (prenexing) {
        circuit_to_prenex(circuit);
    }
    
    if (output == NULL) {
        circuit_open_and_write_qcir_file(circuit, output_file_name);
    } else {
        circuit_print_qcir(circuit);
    }
    
    circuit_free(circuit);
    
    return 0;
}
