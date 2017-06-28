//
//  certcheck.c
//  caqe-qcir
//
//  Created by Leander Tentrup on 07.01.16.
//  Copyright © 2016 Saarland University. All rights reserved.
//

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#include "solver.h"
#include "circuit.h"
#include "qcir.h"
#include "logging.h"
#include "map.h"
#include "aiger.h"
#include "util.h"
#include "getopt.h"
#include "queue.h"


typedef struct {
    Circuit* circuit;
    qbf_res result;
    aiger* cert;
    aiger* combined;
    unsigned next_aiger_lit;
} CertCheck;

static qbf_res certificate_get_type(aiger* certificate) {
    // the last output of certificate encodes whether its a Skolem or Herbrand certificate
    aiger_symbol strategy_type = certificate->outputs[certificate->num_outputs - 1];
    if (strategy_type.lit == 0) {
        return QBF_RESULT_UNSAT;
    } else {
        assert(strategy_type.lit == 1);
        return QBF_RESULT_SAT;
    }
}

static CertCheck* certcheck_init(Circuit* circuit, aiger* cert) {
    CertCheck* check = malloc(sizeof(CertCheck));
    check->circuit = circuit;
    check->next_aiger_lit = circuit->max_num + 1;
    check->result = certificate_get_type(cert);
    check->cert = cert;
    check->combined = aiger_init();
    return check;
}

/**
 * Translates matrix literals to AIGER literals.
 *
 * Example: lit = -5 => return 5 * 2 + 1
 */
static unsigned circuit_lit_to_aiger_lit(lit_t lit) {
    if (lit < 0) {
        lit = -lit;
        return (unsigned)lit * 2 + 1;
    }
    return (unsigned)lit * 2;
}

static char* int2str(int i) {
    size_t buffer_size = (((size_t)ceil(log10(i)) + 1) * sizeof(char));
    char* str = malloc(buffer_size);
    sprintf(str, "%d", i);
    return str;
}

static unsigned new_aiger_lit(CertCheck* check) {
    return ++check->next_aiger_lit * 2;
}

/**
 * Encodes the AND of aiger literals given in int_vector such that the result
 * is stored in dest_aiger_lit
 *
 * @note modifies aiger_lits
 */
static void encode_and_as(CertCheck* check, int_queue* aiger_lits, unsigned dest_aiger_lit) {
    while (int_queue_has_at_least_two_elements(aiger_lits)) {
        unsigned lhs = (unsigned)int_queue_pop(aiger_lits);
        unsigned rhs = (unsigned)int_queue_pop(aiger_lits);
        unsigned new_lit = new_aiger_lit(check);
        aiger_add_and(check->combined, new_lit, lhs, rhs);
        int_queue_push(aiger_lits, (int)new_lit);
    }
    unsigned lhs = aiger_true, rhs = aiger_true;
    
    if (int_queue_has_at_least_one_element(aiger_lits)) {
        lhs = (unsigned)int_queue_pop(aiger_lits);
    }
    if (int_queue_has_at_least_one_element(aiger_lits)) {
        rhs = (unsigned)int_queue_pop(aiger_lits);
    }
    aiger_add_and(check->combined, aiger_strip(dest_aiger_lit), lhs, rhs);
}
/**
 * Encodes the OR of aiger literals given in int_vector such that the result
 * is stored in dest_aiger_lit
 *
 * @note modifies aiger_lits
 */
static void encode_or_as(CertCheck* check, int_queue* aiger_lits, unsigned dest_aiger_lit) {
    // negate literals in aiger_lits first
    for (int_queue_element* element = aiger_lits->first; element != NULL; element = element->next) {
        element->value = (int)aiger_not(element->value);
    }
    encode_and_as(check, aiger_lits, dest_aiger_lit);
}

static void import_variables(CertCheck* check) {
    quantifier_type func_type = check->result == QBF_RESULT_SAT ? QUANT_EXISTS : QUANT_FORALL;
    for (size_t i = 0; i < vector_count(check->circuit->vars); i++) {
        Var* var = vector_get(check->circuit->vars, i);
        if (var->scope->qtype == func_type) {
            continue;
        }
        aiger_add_input(check->combined, circuit_lit_to_aiger_lit(var->shared.id), int2str(var->shared.id));
    }
}

static unsigned circuit_gate_to_aiger_lit(Gate* gate) {
    unsigned base = circuit_lit_to_aiger_lit(gate->shared.id);
    if (gate->type == GATE_AND) {
        return base;
    } else {
        assert(gate->type == GATE_OR);
        // reference OR gates only negated, since or(x,y) = !and(!x,!y)
        return aiger_not(base);
    }
}

static void import_gate(CertCheck* check, Gate* gate, int_queue* gate_aiger_inputs) {
    assert(int_queue_is_empty(gate_aiger_inputs));
    
    const Circuit* circuit = check->circuit;
    
    for (size_t j = 0; j < gate->num_inputs; j++) {
        lit_t lit = gate->inputs[j];
        var_t var = lit_to_var(lit);
        const node_type type = circuit->types[var];
        if (type == NODE_VAR) {
            int_queue_push(gate_aiger_inputs, circuit_lit_to_aiger_lit(lit));
        } else if (type == NODE_SCOPE) {
            ScopeNode* scope_node = circuit->nodes[var];
            unsigned aiger_lit = circuit_lit_to_aiger_lit(scope_node->shared.id);
            int_queue_push(gate_aiger_inputs, aiger_lit);
        } else {
            assert(type == NODE_GATE);
            Gate* other_gate = circuit->nodes[var];
            unsigned aiger_lit = circuit_gate_to_aiger_lit(other_gate);
            if (lit < 0) {
                aiger_lit = aiger_not(aiger_lit);
            }
            int_queue_push(gate_aiger_inputs, aiger_lit);
        }
    }
    
    const unsigned aiger_gate_lit = circuit_lit_to_aiger_lit(gate->shared.id);
    
    if (gate->type == GATE_AND) {
        encode_and_as(check, gate_aiger_inputs, aiger_gate_lit);
    } else {
        assert(gate->type == GATE_OR);
        encode_or_as(check, gate_aiger_inputs, aiger_gate_lit);
    }
}

static void import_scope_node(CertCheck* check, ScopeNode* scope_node) {
    const unsigned aiger_scope_node_lit = circuit_lit_to_aiger_lit(scope_node->shared.id);
    const unsigned aiger_sub_lit = circuit_lit_to_aiger_lit(scope_node->sub);
    aiger_add_and(check->combined, aiger_scope_node_lit, aiger_sub_lit, aiger_true);
}

/**
 * Import the negated circuit into aiger instance check->combined
 */
static void import_circuit(CertCheck* check) {
    const Circuit* circuit = check->circuit;
    
    import_variables(check);
    
    int_queue gate_aiger_inputs;
    int_queue_init(&gate_aiger_inputs);
    
    for (size_t i = 1; i <= circuit->max_num; i++) {
        const node_type type = circuit->types[i];
        if (type == NODE_VAR || type == 0) {
            continue;
        } else if (type == NODE_SCOPE) {
            ScopeNode* scope_node = circuit->nodes[i];
            import_scope_node(check, scope_node);
        } else {
            Gate* gate = circuit->nodes[i];
            import_gate(check, gate, &gate_aiger_inputs);
        }
    }
    assert(int_queue_is_empty(&gate_aiger_inputs));
    
    // set output
    var_t output_var = lit_to_var(circuit->output);
    assert(circuit->types[output_var] == NODE_GATE);
    Gate* output_gate = circuit->nodes[output_var];
    unsigned aiger_lit = circuit_gate_to_aiger_lit(output_gate);
    if (circuit->output < 0) {
        aiger_lit = aiger_not(aiger_lit);
    }
    if (check->result == QBF_RESULT_SAT) {
        aiger_add_output(check->combined, aiger_not(aiger_lit), "bad");
    } else {
        assert(check->result == QBF_RESULT_UNSAT);
        aiger_add_output(check->combined, aiger_lit, "bad");
    }
}

static unsigned translate_src_input_to_dst_input(aiger_symbol* input, aiger* dst) {
    // need to do linear search through inputs
    for (size_t i = 0; i < dst->num_inputs; i++) {
        aiger_symbol cmp = dst->inputs[i];
        if (strcmp(cmp.name, input->name) == 0) {
            return cmp.lit;
        }
    }
    assert(false);
    return 0;
}

/**
 * Translates a AIGER literal from src to destination.
 *
 * - Constants are not touched
 * - Inputs are translated using translate_src_input_to_dst_input
 * - ANDs are translated using the given offset
 */
static unsigned aiger_lit_translate(aiger* src, aiger* dest, unsigned lit, unsigned offset) {
    aiger_symbol* input = NULL;
    if (aiger_strip(lit) == 0) {
        // constant
        return lit;
    } else if ((input = aiger_is_input(src, aiger_strip(lit)))) {
        // input
        int translated = translate_src_input_to_dst_input(input, dest);
        return (aiger_strip(lit) == lit) ? translated : aiger_not(translated);
    } else {
        return lit + offset;
    }
}

static void import_and_gates(aiger* src, aiger* dest, unsigned offset) {
    for (unsigned i = 0; i < src->num_ands; i++) {
        aiger_and and_gate = src->ands[i];
        
        unsigned shifted_and = and_gate.lhs + offset;
        unsigned shifted_lhs = aiger_lit_translate(src, dest, and_gate.rhs0, offset);
        unsigned shifted_rhs = aiger_lit_translate(src, dest, and_gate.rhs1, offset);
        
        aiger_add_and(dest, shifted_and, shifted_lhs, shifted_rhs);
    }
}

static void define_strategies(CertCheck* check) {
    // the last output indicates whether certificate is Skolem/Herbrand so we ignore it here
    for (unsigned i = 0; i < check->cert->num_outputs - 1; i++) {
        aiger_symbol output = check->cert->outputs[i];
        unsigned orig_var = strtol(output.name, NULL, 0);
        assert(orig_var > 0);
        //assert(orig_var <= check->circuit->num_vars);
		
		// proper handle variables in strategy not contained in input (e.g. due to tseitin variables)
		if (orig_var > check->circuit->max_num) {
			continue;
		} else if (check->circuit->types[orig_var] != NODE_VAR) {
			continue;
		}
        
        unsigned output_definition = aiger_lit_translate(check->cert, check->combined, output.lit, check->next_aiger_lit * 2);
        aiger_add_and(check->combined, circuit_lit_to_aiger_lit(orig_var), output_definition, 1);
    }
}

/**
 * Imports circuit representing strategy into check->combined.
 * - Checks that inputs of both circuits are the same
 * - Offsets all AND gates by check->next_aig_lit
 * - Defines AND gates representing strategy variables
 */
static void combine_with_certificate(CertCheck* check) {
    import_and_gates(check->cert, check->combined, check->next_aiger_lit * 2);
    define_strategies(check);
}


static void print_usage(const char* name) {
    printf("usage: %s [-h] instance cert\n\n"
           "optional arguments:\n"
           "  -h, --help\t show this help message and exit\n", name);
}

int main(int argc, char * const argv[]) {
    // Handling of command line arguments
    opterr = 0; // Disable error messages as we handle them ourself
    const char * ch;
    volatile bool print_size = false;
    while ((ch = GETOPT(argc, argv)) != NULL) {
        GETOPT_SWITCH(ch) {
            GETOPT_OPT("-h"):
            print_usage(argv[0]);
            return 1;
            break;
            GETOPT_OPT("-s"):
            print_size = true;
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
    
    if (optind + 2 < argc) {
        logging_error("too many arguments, see --help for more information\n");
        return 1;
    } else if (optind + 1 > argc) {
        logging_error("too few arguments, see --help for more information\n");
        return 1;
    }
    
    const char* instance_name = argv[optind];
    bool use_stdin = optind + 2 > argc;
    const char* cert_name;
    if (!use_stdin) {
        cert_name = argv[optind + 1];
    }
    
    Circuit* circuit = circuit_init();
    if (!circuit) {
        return 1;
    }
    
    int error = circuit_open_and_read_qcir_file(circuit, instance_name, false);
    if (error) {
        return 1;
    }
    
    aiger* cert = aiger_init();
    if (cert == NULL) {
        return 1;
    }
    
    if (use_stdin) {
        if (aiger_read_from_file(cert, stdin)) {
            logging_fatal("cannot read certificate file\n");
            return  1;
        }
    } else {
        if (aiger_open_and_read_from_file(cert, cert_name) != NULL) {
            logging_fatal("cannot read certificate file\n");
            return  1;
        }
    }
    
    if (print_size) {
        fprintf(stderr, "Size: %d\n", cert->num_ands);
    }
    
    CertCheck* check = certcheck_init(circuit, cert);
    import_circuit(check);
    combine_with_certificate(check);
    
    if (aiger_check(check->combined)) {
        printf("%s\n", aiger_error(check->combined));
        abort();
    }
    
    assert(!aiger_check(check->combined));

    aiger_reencode(check->combined);
    aiger_write_to_file(check->combined, aiger_ascii_mode, stdout);
    
    circuit_free(circuit);
    
    return 0;
}

