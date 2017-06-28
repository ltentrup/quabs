//
//  main.c
//  aigwin2qcir
//
//  Created by Leander Tentrup on 18.01.17.
//  Copyright © 2017 Saarland University. All rights reserved.
//

#include <stdio.h>
#include <string.h>

#include "logging.h"
#include "aiger.h"
#include "circuit.h"
#include "circuit_print.h"
#include "map.h"

static void print_usage(const char* name) {
    printf("usage: %s [-h] skolem|herbrand aiger-instance cnf-winning\n\n"
           "optional arguments:\n"
           "  -h, --help\t show this help message and exit\n", name);
}

enum QueryType {
    Herbrand,  // the strategy will be encoded as Herbrand function
    Skolem     // the strategy will be encoded as Skolem function
};

void build_query_from_aig(aiger*, const char* win, enum QueryType);

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
    
    if (current_pos + 3 < argc) {
        logging_error("too many arguments, see --help for more information\n");
        return 1;
    }
    
    if (current_pos + 3 > argc) {
        logging_error("too few arguments, see --help for more information\n");
        return 1;
    }
    
    enum QueryType type;
    if (strcmp(argv[1], "skolem") == 0) {
        type = Skolem;
    } else if (strcmp(argv[1], "herbrand") == 0) {
        type = Herbrand;
    } else {
        logging_error("first argument msut be either \"skolem\" or \"herbrand\"");
        return 1;
    }
    
    aiger* instance = aiger_init();
    const char* error = aiger_open_and_read_from_file(instance, argv[2]);
    if (error) {
        printf("%s\n", aiger_error(instance));
        return 1;
    }
    
    build_query_from_aig(instance, argv[3], type);
    
    return 0;
}

typedef struct {
    aiger* instance;
    Circuit* circuit;
    map* prime_mapping;
    map* unprime_mapping;
    unsigned constant_zero;
    
    Scope* state_inputs;
    Scope* controllable;
    Scope* next_state;
    
    Gate* fixpoint;
    Gate* fixpoint_prime;

} SafetyGame;

static lit_t aig_lit_to_qcir_lit(SafetyGame* fixpoint, unsigned aig_lit) {
    assert(fixpoint->constant_zero != 0);
    if (aig_lit == 0) {
        return (lit_t)fixpoint->constant_zero;
    } else if (aig_lit == 1) {
        return -(lit_t)fixpoint->constant_zero;
    } else if (aig_lit % 2) {
        return -((aig_lit - 1) / 2);
    } else {
        return aig_lit / 2;
    }
}

/**
 * Imports an AIGER circuit into the Circuit data structure.
 * The following conversion is assumed: for an AIGER lit aig_lit,
 * the corresponding Circuit lit is aig_lit/2.
 */
static void import_aiger_circuit(SafetyGame* fixpoint) {
    for (unsigned i = 0; i < fixpoint->instance->num_ands; i++) {
        aiger_and and = fixpoint->instance->ands[i];
        Gate* gate = circuit_add_gate(fixpoint->circuit, aig_lit_to_qcir_lit(fixpoint, and.lhs), GATE_AND);
        circuit_add_to_gate(fixpoint->circuit, gate, aig_lit_to_qcir_lit(fixpoint, and.rhs0));
        circuit_add_to_gate(fixpoint->circuit, gate, aig_lit_to_qcir_lit(fixpoint, and.rhs1));
    }
}

static lit_t prime_latch(aiger* aiger, lit_t latch_id) {
    return latch_id + aiger->maxvar + 2;
}

static void import_aiger_variables(SafetyGame* fixpoint, enum QueryType query) {
    Circuit* circuit = fixpoint->circuit;
    aiger* instance = fixpoint->instance;
    
    for (size_t i = 0; i < instance->num_inputs; i++) {
        aiger_symbol sym = instance->inputs[i];
        if (sym.name && strncmp(sym.name, "controllable_", 13) == 0) {
            // controllable input
            circuit_new_var(circuit, fixpoint->controllable, aig_lit_to_qcir_lit(fixpoint, sym.lit));
        } else {
            // uncontrollable input
            circuit_new_var(circuit, fixpoint->state_inputs, aig_lit_to_qcir_lit(fixpoint, sym.lit));
        }
    }
    
    for (size_t i = 0; i < instance->num_latches; i++) {
        aiger_symbol sym = instance->latches[i];
        int lit = aig_lit_to_qcir_lit(fixpoint, sym.lit);
        Var* var = circuit_new_var(circuit, fixpoint->state_inputs, lit);
        int primed_lit = prime_latch(instance, i);
        Var* primed = circuit_new_var(circuit, query == Herbrand ? fixpoint->next_state : fixpoint->controllable, primed_lit);
        map_add(fixpoint->prime_mapping, lit, primed);
        map_add(fixpoint->unprime_mapping, primed_lit, var);
    }
}

static void build_equality(Circuit* circuit, Gate* and_gate, int lhs, int rhs) {
    // lhs -> rhs
    Gate* imp_pos = circuit_add_gate(circuit, circuit->max_num + 1, GATE_OR);
    circuit_add_to_gate(circuit, and_gate, imp_pos->shared.id);
    circuit_add_to_gate(circuit, imp_pos, -lhs);
    circuit_add_to_gate(circuit, imp_pos, rhs);
    
    // !lhs -> !rhs
    Gate* imp_neg = circuit_add_gate(circuit, circuit->max_num + 1, GATE_OR);
    circuit_add_to_gate(circuit, and_gate, imp_neg->shared.id);
    circuit_add_to_gate(circuit, imp_neg, lhs);
    circuit_add_to_gate(circuit, imp_neg, -rhs);
}

static void read_winning(SafetyGame* fixpoint, const char* winning_cnf, enum QueryType query, Gate* gate, bool prime) {
    FILE* win = fopen(winning_cnf, "r");
    if (!win) {
        logging_fatal("could not open file containing winning region\n");
    }
    // file contains a symbol table before the cnf part
    map* symboltable = map_init();
    
    bool negate_cnf = (query == Herbrand && prime) || (query == Skolem && !prime);
    
    char c;
    int lit = -1;
    bool neg = false;
    Gate* new_gate = NULL;
    while ((c = getc(win)) != EOF) {
        switch (c) {
            case 'c':  // comment
            {
                int cnf_lit = 0;
                int aig_lit = 0;
                char space = getc(win);
                assert(space == ' ');
                char lit = getc(win);
                while ('0' <= lit && lit <= '9') {
                    cnf_lit = cnf_lit * 10 + (lit - '0');
                    lit = getc(win);
                }
                assert(lit == ' ');
                lit = getc(win);
                while ('0' <= lit && lit <= '9') {
                    aig_lit = aig_lit * 10 + (lit - '0');
                    lit = getc(win);
                }
                // error latch is encoded as 0
                map_add(symboltable, cnf_lit, aig_lit != 0 ? aig_lit : fixpoint->instance->latches[fixpoint->instance->num_latches - 1].lit);
                break;
            }
            case 'p':  // header
                while (getc(win) != '\n');
                break;
                
            case ' ':  // spaces
            case '\n':
                if (lit == -1) {
                    break;
                }
                if (new_gate == NULL) {
                    new_gate = circuit_add_gate(fixpoint->circuit, fixpoint->circuit->max_num + 1, negate_cnf ? GATE_AND : GATE_OR);
                }
                if (lit == 0) {
                    logging_debug("0\n");
                    circuit_add_to_gate(fixpoint->circuit, gate, new_gate->shared.id);
                    new_gate = NULL;
                } else {
                    assert(lit >= 0);
                    logging_debug("%d ", neg ? -lit : lit);
                    
                    assert(map_get(symboltable, lit) != NULL);
                    int aig_lit = map_get(symboltable, lit);
                    int encoded = aig_lit_to_qcir_lit(fixpoint, aig_lit);
                    if (prime) {
                        assert(map_contains(fixpoint->prime_mapping, encoded));
                        Var* primed = map_get(fixpoint->prime_mapping, encoded);
                        encoded = primed->shared.id;
                    }
                    encoded = neg ? -encoded : encoded;
                    
                    circuit_add_to_gate(fixpoint->circuit, new_gate, negate_cnf ? -encoded : encoded);
                }
                lit = -1;
                neg = false;
                break;
                
            case '-':
                assert(neg == false);
                neg = true;
                break;
                
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                
                if (lit == -1) {
                    lit = 0;
                }
                lit = (lit * 10) + (c - '0');
                break;
                
            default:  // literals
                logging_fatal("Unknown character \"%c\"\n", c);
        }
    }
}

/**
 * Depending on `query`, it builds the following queries:
 * - Herbrand:  F /\ T /\ !F'
 * - Skolem: F -> (T /\ F')
 * - F = winning_region
 * - T = /\_l (l' <-> f_l)
 */
static void build_qbf_query(SafetyGame* fixpoint, const char* winning_cnf, enum QueryType query) {
    Circuit* circuit = fixpoint->circuit;
    aiger* instance = fixpoint->instance;
    
    Gate* top_level = circuit_add_gate(circuit, circuit->max_num + 1, query == Herbrand ? GATE_AND : GATE_OR);
    circuit_set_output(circuit, top_level->shared.id);
    //circuit_add_to_gate(circuit, implication_gate, -aig_lit_to_qcir_lit(fixpoint, instance->outputs[0].lit));
    
    // F
    Gate* f = circuit_add_gate(circuit, circuit->max_num + 1, query == Herbrand ? GATE_AND : GATE_OR);
    circuit_add_to_gate(circuit, top_level, f->shared.id);
    read_winning(fixpoint, winning_cnf, query, f, false);
    
    /*circuit_add_to_gate(circuit, f, aig_lit_to_qcir_lit(fixpoint, instance->latches[instance->num_latches-1].lit));
    f->keep = true;
    circuit_add_to_gate(circuit, implication_gate, f->shared.id);
    fixpoint->fixpoint = f;*/
    //circuit_add_to_gate(circuit, implication_gate, aig_lit_to_qcir_lit(fixpoint, instance->latches[instance->num_latches-1].lit));
    
    // T
    Gate* transition_gate = circuit_add_gate(circuit, circuit->max_num + 1, GATE_AND);
    circuit_add_to_gate(circuit, top_level, transition_gate->shared.id);
    
    //circuit_add_to_gate(circuit, transition_gate, -aig_lit_to_qcir_lit(fixpoint, instance->outputs[0].lit));
    for (size_t i = 0; i < instance->num_latches; i++) {
        aiger_symbol sym = instance->latches[i];
        if (sym.next == 1) {
            circuit_add_to_gate(circuit, transition_gate, prime_latch(instance, i));
        } else {
            build_equality(circuit, transition_gate, prime_latch(instance, i), aig_lit_to_qcir_lit(fixpoint, sym.next));
        }
    }
    
    // F'
    Gate* f_prime = circuit_add_gate(circuit, circuit->max_num + 1, query == Herbrand ? GATE_OR : GATE_AND);
    circuit_add_to_gate(circuit, query == Herbrand ? top_level : transition_gate, f_prime->shared.id);
    read_winning(fixpoint, winning_cnf, query, f_prime, true);
    /*circuit_add_to_gate(circuit, f_prime, -prime_latch(instance, instance->num_latches-1));
    f_prime->keep = true;
    circuit_add_to_gate(circuit, transition_gate, f_prime->shared.id);
    //circuit_add_to_gate(circuit, transition_gate, -prime_latch(instance, instance->num_latches-1));
    fixpoint->fixpoint_prime = f_prime;*/
}

void build_query_from_aig(aiger* instance, const char* winning_cnf, enum QueryType query) {
    SafetyGame* fixpoint = malloc(sizeof(SafetyGame));
    
    fixpoint->instance = instance;
    fixpoint->circuit = circuit_init();
    
    fixpoint->prime_mapping = map_init();
    fixpoint->unprime_mapping = map_init();
    
    // add fake error latch, takes the single output as input
    aiger_add_latch(instance, (instance->maxvar + 1) * 2, instance->outputs[0].lit, "fake error latch");
    
    // add constant zero
    fixpoint->constant_zero = instance->maxvar + 1;
    circuit_add_gate(fixpoint->circuit, fixpoint->constant_zero, GATE_OR);
    
    if (query == Herbrand) {
        fixpoint->state_inputs = circuit_init_scope(fixpoint->circuit, QUANT_EXISTS);
        fixpoint->controllable = circuit_init_scope(fixpoint->circuit, QUANT_FORALL);
        fixpoint->next_state = circuit_init_scope(fixpoint->circuit, QUANT_EXISTS);
    } else {
        fixpoint->state_inputs = circuit_init_scope(fixpoint->circuit, QUANT_FORALL);
        fixpoint->controllable = circuit_init_scope(fixpoint->circuit, QUANT_EXISTS);
        fixpoint->next_state = NULL;
    }
    
    import_aiger_variables(fixpoint, query);
    import_aiger_circuit(fixpoint);
    
    build_qbf_query(fixpoint, winning_cnf, query);
    
    //circuit_reencode(fixpoint->circuit);
    
    circuit_print_qcir(fixpoint->circuit);
}
