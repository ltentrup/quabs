//
//  main.c
//  caqe-qcir
//
//  Created by Leander Tentrup on 01.07.15.
//  Copyright (c) 2015 Saarland University. All rights reserved.
//

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "getopt.h"

#include "solver.h"
#include "qcir.h"
#include "logging.h"
#include "statistics.h"
#include "certification.h"

static void print_usage(const char* name) {
    printf("usage: %s [options] file\n"
           "options:\n"
#ifdef CERTIFICATION
           "  -c                        enable certification\n"
#endif
           "  -v                        enable verbose output\n"
           "  --preprocessing 1/0       enable/disable preprocessing (default 1)\n"
           "  --miniscoping 1/0         enable/disable miniscoping (default 0)\n"
           "  --statistics              show collected solving statistics\n"
           "  --partial-assignment      print satisfying assignment of outermost quantifier\n"
           "  --assignment-minimization mimimize abstraction entries based on assignments\n"
#ifdef PARALLEL_SOLVING
           "  --num-threads N           number of threads to use during solving (default 2)\n"
#endif
           "  -h/--help                 show this message and exit\n", name);
}

static bool parse_boolean_argument(const char* cmd, const char* arg) {
    if (strlen(arg) != 1 || (arg[0] != '1' && arg[0] != '0')) {
        logging_fatal("Wrong argument %s for %s, expect 0/1\n", arg, cmd);
    }
    return arg[0] == '1';
}

int main(int argc, char * const argv[]) {
    
    //logging_print("caqe (QCIR version)\n");
    
    const char *file_name = NULL;
    FILE* file = NULL;
    SolverOptions* options = solver_get_default_options();
    size_t max_num = 0;
    
    // Handling of command line arguments
    const char * ch;
    while ((ch = GETOPT(argc, argv)) != NULL) {
        GETOPT_SWITCH(ch) {
        GETOPT_OPT("-h"):
        GETOPT_OPT("--help"):
            print_usage(argv[0]);
            return 0;
            break;
        GETOPT_OPT("-v"):
            logging_set_verbosity(VERBOSITY_ALL);
            break;
        
        GETOPT_OPTARG("--preprocessing"):
            options->preprocess = parse_boolean_argument(ch, optarg);
            break;
        GETOPT_OPTARG("--miniscoping"):
            options->miniscoping = parse_boolean_argument(ch, optarg);
            break;
#ifdef CERTIFICATION
        GETOPT_OPT("-c"):
            options->certify = 1;
            break;
#endif
        GETOPT_OPT("--statistics"):
            options->statistics = 1;
            break;
        GETOPT_OPT("--partial-assignment"):
            options->partial_assignment = true;
            break;
        GETOPT_OPTARG("--num-variables"):
            max_num = (int)strtol(optarg, NULL, 0);
            if (max_num <= 0) {
                logging_error("Illegal num-variables argument %d\n", max_num);
                print_usage(argv[0]);
                return 1;
            }
            break;

            
        GETOPT_OPTARG("--assignment-minimization"):
            options->assignment_b_lit_minimization = parse_boolean_argument(ch, optarg);
            break;
        GETOPT_OPTARG("--use-partial-deref"):
            options->use_partial_deref = parse_boolean_argument(ch, optarg);
            break;
        
#ifdef PARALLEL_SOLVING
        GETOPT_OPTARG("--num-threads"):
            options->num_threads = strtoul(optarg, NULL, 0);
            if (options->num_threads == 0) {
                logging_error("Illegal number of threads argument %zu\n", options->num_threads);
                print_usage(argv[0]);
                return 1;
            }
            break;
#endif
        GETOPT_MISSING_ARG:
            printf("missing argument to %s\n", ch);
            /* FALLTHROUGH */
        GETOPT_DEFAULT:
            printf("unknown argument %s\n", ch);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    if (!options->preprocess) {
        logging_warn("Preprocessing is disabled, this will likely harm solving performance\n");
    }
    
    if (optind < argc) {
        file_name = argv[optind];
        if (optind + 1 < argc) {
            logging_warn("More than one positional argument given, see --help for more information\n");
        }
        for (int i = optind + 1; i < argc; i++) {
            logging_warn("Positional argument \"%s\" is ignored\n", argv[i]);
        }
    } else {
        //logging_print("Reading from stdin\n");
        file = stdin;
    }
    
    Stats* parsing_time = statistics_init(10000);
    statistics_start_timer(parsing_time);
    Circuit* circuit = circuit_init();
    bool ignore_qcir_header = false;
    if (max_num > 0) {
        circuit_adjust(circuit, max_num);
        ignore_qcir_header = true;
    }
    int error;
    if (file == NULL) {
        error = circuit_open_and_read_qcir_file(circuit, file_name, ignore_qcir_header);
    } else {
        error = circuit_from_qcir(circuit, file, ignore_qcir_header);
    }
    statistics_stop_and_record_timer(parsing_time);
    if (error) {
        return 1;
    }
    
    Solver* solver = solver_init(options, circuit);
    if (!solver) {
        return 1;
    }
    
    qbf_res res = solver_sat(solver);
    
    if (options->statistics) {
        printf("Parsing took ");
        statistics_print_time(parsing_time);
        solver_print_statistics(solver);
    }
    statistics_free(parsing_time);
    
    assert(res != QBF_RESULT_UNKNOWN);
    //certification_print_result(solver, res);
    
#ifdef CERTIFICATION
    if (options->certify) {
        certification_print(&solver->cert, res);
    }
#endif
    
    if (!options->certify) {
        if (res == QBF_RESULT_SAT) {
            printf("r SAT\n");
        } else {
            printf("r UNSAT\n");
        }
    }
    
    return res;
}
