//
//  realizer.c
//  realizer
//
//  Created by Leander Tentrup on 30.11.15.
//  Copyright © 2015 Saarland University. All rights reserved.
//

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "fixpoint.h"
#include "solver.h"
#include "circuit.h"
#include "circuit_print.h"
#include "qcir.h"
#include "logging.h"
#include "aiger.h"
#include "map.h"
#include "getopt.h"

int constant_zero = 0;
Gate* implication_gate = NULL;
Gate* transition_gate = NULL;

void print_usage(const char* name) {
    printf("usage: %s [-h] file\n\n"
           "optional arguments:\n"
           "  -h, --help\t show this help message and exit\n", name);
}

int main(int argc, char * const argv[]) {
    const char *file_name;
    FILE* file;
    volatile bool print_qcir = false;
    volatile bool print_statistics = false;
    
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
            // Pint intermediary QCIR files
            print_qcir = true;
            break;
        GETOPT_OPT("--statistics"):
            print_statistics = true;
            break;
        GETOPT_OPT("-v"):
            logging_set_verbosity(VERBOSITY_ALL);
            break;
        GETOPT_MISSING_ARG:
            printf("missing argument to %s\n", ch);
            /* FALLTHROUGH */
        GETOPT_DEFAULT:
            printf("%s", ch);
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
        file = fopen(file_name, "r");
        if (!file) {
            logging_error("File \"%s\" does not exist!\n", file_name);
            return 1;
        }
    } else {
        logging_print("Reading from stdin\n");
        file = stdin;
    }
    
    aiger* aig = aiger_init();
    const char* error = aiger_read_from_file(aig, file);
    if (error) {
        logging_fatal("Could not read AIGER: \"%s\"\n", aiger_error(aig));
    }
    fclose(file);
    
    fixpoint_aiger_preprocess(aig);
    
    Fixpoint* fixpoint = fixpoint_init_from_aiger(aig);
    
    //circuit_print_qcir(fixpoint->circuit);
    fixpoint_res result = fixpoint_solve(fixpoint);
    
    if (result == FIXPOINT_INDUCTIVE) {
        printf("REALIZABLE\n");
    } else {
        assert(result == FIXPOINT_NO_FIXPOINT);
        printf("UNREALIZABLE\n");
    }
    
    if (print_statistics) {
        printf("Number of exclusions\n");
        statistics_print(fixpoint->exclusions);
        
        printf("Number of refinements\n");
        statistics_print(fixpoint->num_refinements);
        
        printf("Cube sizes\n");
        statistics_print(fixpoint->cube_size);
        
        printf("Outer SAT solving\n");
        statistics_print(fixpoint->outer_sat);
        
        printf("Inner SAT solving\n");
        statistics_print(fixpoint->inner_sat);
        
        printf("SAT refinements\n");
        statistics_print(fixpoint->sat_refinements);
        
        printf("UNSAT refinements\n");
        statistics_print(fixpoint->unsat_refinements);
    }
    
    return 0;
}
