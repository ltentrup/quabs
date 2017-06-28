//
//  certification.c
//  caqe-qcir
//
//  Created by Leander Tentrup on 06.01.16.
//  Copyright © 2016 Saarland University. All rights reserved.
//

#include "certification.h"

#include "aiger_helper.h"
#include "circuit_abstraction.h"

static char buffer[1024] = { 0 };

static const char* int2str(unsigned literal) {
    snprintf(buffer, 1024, "%d", literal);
    return buffer;
}

void certification_init(certification* cert, Circuit* circuit) {
    cert->skolem = aiger_init();
    cert->herbrand = aiger_init();
    cert->next_lit_skolem = (circuit->max_scope_id + 1) * circuit->max_num;
    cert->next_lit_herbrand = (circuit->max_scope_id + 1) * circuit->max_num;
    
    int_queue_init(&cert->queues[0]);
    int_queue_init(&cert->queues[1]);
    cert->current_queue = 0;
    
    cert->function_lit = map_init();
    cert->precondition_lit = map_init();
}

static void import_variables_recursive(certification* cert, Scope* scope) {
    // add variables as inputs to certificate
    for (size_t i = 0; i < vector_count(scope->vars); i++) {
        Var* var = vector_get(scope->vars, i);
        if (scope->qtype == QUANT_EXISTS) {
            aiger_add_output(cert->skolem, var->shared.id * 2, int2str(var->shared.orig_id));
            aiger_add_input(cert->herbrand, var->shared.id * 2, int2str(var->shared.orig_id));
        } else {
            assert(scope->qtype == QUANT_FORALL);
            aiger_add_output(cert->herbrand, var->shared.id * 2, int2str(var->shared.orig_id));
            aiger_add_input(cert->skolem, var->shared.id * 2, int2str(var->shared.orig_id));
        }
        
        map_add(cert->function_lit, var->shared.id, aiger_false);
        map_add(cert->precondition_lit, var->shared.id, aiger_false);
    }
    
    for (size_t i = 0; i < scope->num_next; i++) {
        import_variables_recursive(cert, scope->next[i]);
    }
}

void certification_import_variables(certification* cert, Circuit* circuit) {
    import_variables_recursive(cert, circuit->top_level);
    
    // import variables that were removed
    for (size_t i = 0; i < vector_count(circuit->vars); i++) {
        Var* var = vector_get(circuit->vars, i);
        if (var->removed) {
            unsigned value = (var->shared.value > 0) ? aiger_true : aiger_false;
            if (var->orig_quant == QUANT_EXISTS) {
                aiger_add_output(cert->skolem, value, int2str(var->shared.orig_id));
                //aiger_add_input(cert->herbrand, var->shared.id * 2, int2str(var->shared.orig_id));
            } else {
                aiger_add_output(cert->herbrand, value, int2str(var->shared.orig_id));
                //aiger_add_input(cert->skolem, var->shared.id * 2, int2str(var->shared.orig_id));
            }
        }
    }

}

static unsigned circuit_lit_to_aiger_lit(lit_t literal) {
    if (literal < 0) {
        literal = -literal;
        return aiger_not((unsigned)literal * 2);
    }
    return (unsigned)literal * 2;
}

void certification_add_literal(certification* cert, lit_t literal) {
    int_queue_push(&cert->queues[cert->current_queue], circuit_lit_to_aiger_lit(literal));
}

void certification_add_aiger_literal(certification* cert, lit_t aiger_lit) {
    int_queue_push(&cert->queues[cert->current_queue], aiger_lit);
}

static unsigned translate_gate_to_b_literal(CircuitAbstraction* abs, quantifier_type qtype, var_t gate_id) {
    const Circuit* circuit = abs->scope->circuit;
    unsigned offset = (abs->scope->scope_id - 1) * circuit->max_num;
    unsigned literal = offset + gate_id;
    unsigned aiger_literal = literal * 2;
    
    Gate* gate = circuit->nodes[gate_id];
    assert(gate != NULL);
    if (normalize_gate_type(gate->type, qtype) == GATE_OR) {
        aiger_literal = aiger_not(aiger_literal);
    }
    return aiger_literal;
}

void certification_add_b_literal(certification* cert, CircuitAbstraction* abs, quantifier_type qtype, var_t gate_id) {
    unsigned translated_b_lit = translate_gate_to_b_literal(abs, qtype, gate_id);
    int_queue_push(&cert->queues[cert->current_queue], translated_b_lit);
}

void certification_add_t_literal(certification* cert, CircuitAbstraction* abs, quantifier_type qtype, var_t gate_id, lit_t b_lit) {
    // traverse scopes until we hit an abstraction that has a b-literal corresponding to gate_id
    for (CircuitAbstraction* outer = abs->prev; outer != NULL; outer = outer->prev) {
        if (!int_vector_contains_sorted(outer->b_lits, b_lit)) {
            assert(outer->prev != NULL);
            continue;
        }
        int_queue_push(&cert->queues[cert->current_queue], translate_gate_to_b_literal(outer, qtype, gate_id));
        return;
    }
}

void certification_define_b_literal(certification* cert, CircuitAbstraction* abs, quantifier_type qtype, var_t gate_id) {
    aiger* base = (qtype == QUANT_EXISTS) ? cert->skolem : cert->herbrand;
    unsigned* next_lit = (qtype == QUANT_EXISTS) ? &cert->next_lit_skolem : &cert->next_lit_herbrand;
    
    const unsigned translated_b_lit = translate_gate_to_b_literal(abs, qtype, gate_id);
    Gate* gate = abs->scope->circuit->nodes[gate_id];
    assert(gate != NULL);
    gate_type type = normalize_gate_type(gate->type, qtype);
    if (type == GATE_OR) {
        encode_or_as(base, next_lit, &cert->queues[cert->current_queue], aiger_strip(translated_b_lit));
    } else {
        assert(type == GATE_AND);
        encode_and_as(base, next_lit, &cert->queues[cert->current_queue], translated_b_lit);
    }
}

unsigned certification_define_or(certification* cert, quantifier_type qtype) {
    aiger* base = (qtype == QUANT_EXISTS) ? cert->skolem : cert->herbrand;
    unsigned* next_lit = (qtype == QUANT_EXISTS) ? &cert->next_lit_skolem : &cert->next_lit_herbrand;
    
    return encode_or(base, next_lit, &cert->queues[cert->current_queue]);
}

unsigned certification_define_and(certification* cert, quantifier_type qtype) {
    aiger* base = (qtype == QUANT_EXISTS) ? cert->skolem : cert->herbrand;
    unsigned* next_lit = (qtype == QUANT_EXISTS) ? &cert->next_lit_skolem : &cert->next_lit_herbrand;
    
    return encode_and(base, next_lit, &cert->queues[cert->current_queue]);
}

void certification_push(certification* cert) {
    cert->current_queue++;
    assert(cert->current_queue < 2);
}

void certification_pop(certification* cert) {
    assert(cert->current_queue > 0);
    cert->current_queue--;
}

void certification_clear(certification* cert) {
    while (!int_queue_is_empty(&cert->queues[cert->current_queue])) {
        int_queue_pop(&cert->queues[cert->current_queue]);
    }
}

bool certification_queue_is_empty(certification* cert) {
    return int_queue_is_empty(&cert->queues[cert->current_queue]);
}

void certification_append_function_case(certification* cert, CircuitAbstraction* abs) {
    unsigned current_precondition = certification_define_and(cert, abs->scope->qtype);
    for (size_t i = 0; i < vector_count(abs->scope->vars); i++) {
        const Var* var = vector_get(abs->scope->vars, i);
        bool negated = (var->shared.value <= 0);
        unsigned function_lit = (uintptr_t)map_get(cert->function_lit, var->shared.id);
        unsigned last_precondition = (uintptr_t)map_get(cert->precondition_lit, var->shared.id);
        
        if (!negated) {
            // !last_precondition && precondition_aiger_lit => x
            assert(int_queue_is_empty(&cert->queues[cert->current_queue]));
            int_queue_push(&cert->queues[cert->current_queue], aiger_not(last_precondition));
            int_queue_push(&cert->queues[cert->current_queue], current_precondition);
            unsigned function_case = certification_define_and(cert, abs->scope->qtype);
            
            // function_lit |= function_case
            assert(int_queue_is_empty(&cert->queues[cert->current_queue]));
            int_queue_push(&cert->queues[cert->current_queue], aiger_not(function_lit));
            int_queue_push(&cert->queues[cert->current_queue], aiger_not(function_case));
            unsigned new_function_lit = certification_define_and(cert, abs->scope->qtype);
            map_update(cert->function_lit, var->shared.id, (void*)(uintptr_t)aiger_not(new_function_lit));
        }
        
        // last_precondition |= precondition_aiger_lit
        assert(int_queue_is_empty(&cert->queues[cert->current_queue]));
        int_queue_push(&cert->queues[cert->current_queue], aiger_not(last_precondition));
        int_queue_push(&cert->queues[cert->current_queue], aiger_not(current_precondition));
        unsigned new_precondition = certification_define_and(cert, abs->scope->qtype);
        
        map_update(cert->precondition_lit, var->shared.id, (void*)(uintptr_t)aiger_not(new_precondition));
    }
}

void certification_define_outputs(certification* cert, CircuitAbstraction* abs, qbf_res result) {
    aiger* strategy = (abs->scope->qtype == QUANT_EXISTS) ? cert->skolem : cert->herbrand;
    if ((abs->scope->qtype == QUANT_EXISTS && result == QBF_RESULT_SAT)
        || (abs->scope->qtype == QUANT_FORALL && result == QBF_RESULT_UNSAT)) {
        
        for (size_t i = 0; i < vector_count(abs->scope->vars); i++) {
            Var* var = vector_get(abs->scope->vars, i);
            
            // define output as AND gate such that outer variables can depend on it
            unsigned function_lit = (uintptr_t)map_get(cert->function_lit, var->shared.id);
            aiger_add_and(strategy, var->shared.id * 2, function_lit, 1);
        }
    }
    
    for (size_t i = 0; i < abs->scope->num_next; i++) {
        certification_define_outputs(cert, abs->next[i], result);
    }
}

void certification_print(certification* cert, qbf_res result) {
    aiger* strategy = (result == QBF_RESULT_SAT) ? cert->skolem : cert->herbrand;
    if (aiger_check(strategy)) {
        printf("%s\n", aiger_error(strategy));
        abort();
    }
    unsigned result_output = (result == QBF_RESULT_SAT) ? aiger_true : aiger_false;
    aiger_add_output(strategy, result_output, "result");
    if (result == QBF_RESULT_SAT) {
        aiger_add_comment(strategy, "SAT");
    } else {
        assert(result == QBF_RESULT_UNSAT);
        aiger_add_comment(strategy, "UNSAT");
    }
    aiger_reencode(strategy);
    aiger_write_to_file(strategy, aiger_ascii_mode, stdout);
}
