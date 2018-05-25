//
//  qcir2qdimacs.c
//  caqe-qcir
//
//  Created by Leander Tentrup on 20.11.15.
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

void print_usage(const char* name) {
    printf("usage: %s [-h] file\n\n"
           "optional arguments:\n"
           "  -h, --help\t show this help message and exit\n", name);
}

int main(int argc, char * const argv[]) {
    const char *file_name = NULL;
    FILE* file = NULL;
    bool preprocess = false;
    
    // Handling of command line arguments
    opterr = 0; // Disable error messages as we handle them ourself
    const char * ch;
    while ((ch = GETOPT(argc, argv)) != NULL) {
        GETOPT_SWITCH(ch) {
            GETOPT_OPT("-h"):
            print_usage(argv[0]);
            return 1;
            break;
            GETOPT_OPT("-p"):
            preprocess = true;
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
    
    if (optind + 1 < argc) {
        logging_error("too many arguments, see --help for more information\n");
        return 1;
    }
    
    if (optind < argc) {
        file_name = argv[optind];
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
    if (preprocess) {
        circuit_reencode(circuit);
        circuit_preprocess(circuit);
    }
    
    circuit_print_qdimacs(circuit);
    
    circuit_free(circuit);
    
    return 0;
}
