//
//  qcir.c
//  caqe-qcir
//
//  Created by Leander Tentrup on 01.07.15.
//  Copyright (c) 2015 Saarland University. All rights reserved.
//

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

#include "qcir.h"
#include "circuit_print.h"
#include "util.h"
#include "logging.h"

#define GUNZIP "gunzip -c %s 2>/dev/null"

#define LINE_BUFFER_SIZE 4096

typedef enum {
    NONE = 0,
    
    // quantifier
    FORALL  = 1 << 0,
    EXISTS  = 1 << 1,
    FREE    = 1 << 2,
    OUTPUT  = 1 << 3,
    
    // literal
    VARIABLE = 1 << 4,
    LITERAL  = 1 << 5,
    
    // syntax
    ASSIGN       = 1 << 6,
    COMMA        = 1 << 7,
    SEMICOLON    = 1 << 8,
    LPAREN       = 1 << 9,
    RPAREN       = 1 << 10,
    LINE_COMMENT = 1 << 11,
    HEADER       = 1 << 12,
    SYMBOLTABLE  = 1 << 13,
    
    LINE_END = 1 << 14,
    FILE_END = 1 << 15,
    
    // gates
    AND     = 1 << 16,
    OR      = 1 << 17,
} token_type;

typedef struct {
    token_type type;
    union {
        lit_t literal;
        var_t variable;
    };
} token;

static const char* token_to_string(token_type t) {
    switch (t) {
        case NONE:         return "NONE";
        case FORALL:       return "forall";
        case EXISTS:       return "exists";
        case FREE:         return "free";
        case OUTPUT:       return "output";
        case VARIABLE:     return "variable";
        case LITERAL:      return "literal";
        case ASSIGN:       return "=";
        case COMMA:        return ",";
        case SEMICOLON:    return ";";
        case LPAREN:       return "(";
        case RPAREN:       return ")";
        case LINE_COMMENT: return "#";
        case HEADER:       return "#QCIR-14";
        case SYMBOLTABLE:  return "#symboltable";
        case LINE_END:     return "EOL";
        case FILE_END:     return "EOF";
        case AND:          return "and";
        case OR:           return "or";
    }
}

typedef struct {
    FILE* file;
    char* buffer;
    size_t line;
    size_t pos;
    bool eof;
} Lexer;


static void lexer_init(Lexer* lexer, FILE* file) {
    lexer->file = file;
    lexer->buffer = malloc(LINE_BUFFER_SIZE);
    lexer->pos = 0;
    lexer->line = 0;
    
    if (fgets(lexer->buffer, LINE_BUFFER_SIZE, file) == NULL) {
        lexer->eof = true;
    } else {
        lexer->eof = false;
    }
}

static void lexer_deinit(Lexer* lexer) {
    free(lexer->buffer);
}

static token make_token(token_type type) {
    token t = {type, .variable=0};
    return t;
}

static token make_var_token(var_t var) {
    token t = {VARIABLE, .variable=var};
    return t;
}

static token make_lit_token(lit_t lit) {
    token t = {LITERAL, .literal=lit};
    return t;
}

static void proceed(Lexer* lexer, size_t length) {
    assert(length > 0);
    lexer->pos += length;
    assert(lexer->pos < LINE_BUFFER_SIZE);
    if (lexer->buffer[lexer->pos] == '\0') {
        // end of buffer
        if (fgets(lexer->buffer, LINE_BUFFER_SIZE, lexer->file) == NULL) {
            assert(lexer->eof == false);
            lexer->eof = true;
        }
        lexer->pos = 0;
        lexer->line++;
    }
}

static token next_token(Lexer* lexer, token_type expect) {
    if (lexer->eof) {
        assert(expect == NONE || (expect & FILE_END));
        return make_token(FILE_END);
    }
    
    size_t length = 1;
    token_type type;
    var_t var;
    token t;
    
    while (lexer->buffer[lexer->pos] == ' ') {
        proceed(lexer, 1);
    }
    
    switch (lexer->buffer[lexer->pos]) {
        case '#':
            // we return from this case
            proceed(lexer, 1);
            if (lexer->eof) {
                assert(expect == NONE);
                return make_token(FILE_END);
            } else if (expect & LINE_COMMENT) {
                // if user expects line comment, we can build non-standard syntax into QCIR
                return make_token(LINE_COMMENT);
            }
            if (strncmp(lexer->buffer + lexer->pos, "QCIR-G14", 8) == 0) {
                proceed(lexer, 8);
                assert(expect == NONE);
                return make_token(HEADER);
            } else if (strncmp(lexer->buffer + lexer->pos, "symboltable", 11) == 0) {
                proceed(lexer, 11);
                assert(expect & SYMBOLTABLE);
                return make_token(SYMBOLTABLE);
            } else {
                while (lexer->buffer[lexer->pos] != '\n') {
                    proceed(lexer, 1);
                }
                return next_token(lexer, expect);
            }
            assert(false);
            break;

        case '-':
            // we return from this case
            if (expect != NONE && !(expect & LITERAL)) {
                logging_fatal("Parsing error: unexpected token \"%s\" at line %zu:%zu\n", token_to_string(LITERAL), lexer->line + 1, lexer->pos + 1);
            }
            proceed(lexer, 1);
            t = next_token(lexer, VARIABLE);
            t = make_lit_token(-t.literal);
            return t;
            
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
            // we return from this case
            if (expect != NONE && !(expect & VARIABLE)) {
                logging_fatal("Parsing error: unexpected token \"%s\" at line %zu:%zu\n", token_to_string(VARIABLE), lexer->line + 1, lexer->pos + 1);
            }
            var = 0;
            while (lexer->buffer[lexer->pos] >= '0' && lexer->buffer[lexer->pos] <= '9') {
                var = (var * 10) + (var_t)(lexer->buffer[lexer->pos] - '0');
                proceed(lexer, 1);
            }
            t = make_var_token(var);
            return t;
        
        case ' ':
            // whitespace
            assert(false);
            break;
            
        case '\n':
            // end of line
            type = LINE_END;
            break;
            
        case '\0':
            // end of buffer
            assert(false);
            break;
        
        case '=':
            type = ASSIGN;
            break;
        
        case '(':
            type = LPAREN;
            break;
            
        case ')':
            type = RPAREN;
            break;
        
        case ',':
            type = COMMA;
            break;
            
        case ';':
            type = SEMICOLON;
            break;
            
        default:
            if (strncasecmp(lexer->buffer + lexer->pos, "forall", 6) == 0) {
                type = FORALL;
                length = 6;
            } else if (strncasecmp(lexer->buffer + lexer->pos, "exists", 6) == 0) {
                type = EXISTS;
                length = 6;
            } else if (strncasecmp(lexer->buffer + lexer->pos, "output", 6) == 0) {
                type = OUTPUT;
                length = 6;
            } else if (strncasecmp(lexer->buffer + lexer->pos, "free", 4) == 0) {
                type = FREE;
                length = 4;
            } else if (strncasecmp(lexer->buffer + lexer->pos, "and", 3) == 0) {
                type = AND;
                length = 3;
            } else if (strncasecmp(lexer->buffer + lexer->pos, "or", 2) == 0) {
                type = OR;
                length = 2;
            } else {
                logging_fatal("Parsing error: Unknown token at line %zu:%zu\n", lexer->line + 1, lexer->pos + 1);
            }
            break;
    }
    proceed(lexer, length);
    if (expect != NONE && !(type & expect)) {
        logging_fatal("Parsing error: unexpected token \"%s\" at line %zu:%zu\n", token_to_string(type), lexer->line + 1, lexer->pos + 1);
    }
    return make_token(type);
}

static bool has_suffix(const char* string, const char* suffix) {
    if (strlen(suffix) > strlen(string)) {
        return false;
    }
    if (strncmp(string + (strlen(string) - strlen(suffix)), suffix, strlen(suffix)) == 0) {
        return true;
    }
    return false;
}

int circuit_open_and_read_qcir_file(Circuit* circuit, const char* file_name, bool ignore_header) {
    FILE* file;
    bool use_pclose;
    
    if (has_suffix(file_name, ".gz")) {
        use_pclose = true;
        
        char* cmd = malloc(strlen(file_name) + strlen(GUNZIP));
        sprintf(cmd, GUNZIP, file_name);
        file = popen(cmd, "r");
        free(cmd);
    } else {
        file = fopen(file_name, "r");
        use_pclose = false;
    }
    
    if (!file) {
        logging_fatal("File \"%s\" does not exist!\n", file_name);
        return -1;
    }
    
    int error = circuit_from_qcir(circuit, file, ignore_header);
    
    if (use_pclose) {
        pclose(file);
    } else {
        fclose(file);
    }
    
    return error;
}

int circuit_open_and_write_qcir_file(Circuit* circuit, const char* file_name) {
    FILE* file = fopen(file_name, "w");
    if (!file) {
        logging_fatal("Cannot create file \"%s\"!\n", file_name);
        return -1;
    }
    circuit_write_qcir(circuit, file);
    return fclose(file);
}

int circuit_from_qcir(Circuit* circuit, FILE* file, bool ignore_header) {
    Lexer lexer;
    lexer_init(&lexer, file);
    token t = next_token(&lexer, NONE);
    
    while (t.type == LINE_END) {
        t = next_token(&lexer, NONE);
    }
    
    if (t.type == HEADER) {
        t = next_token(&lexer, VARIABLE | LINE_END);
        if (t.type == VARIABLE) {
            assert(t.variable >= 0);
            logging_debug("#GCIR-14 %d\n", t.variable);
            if (!ignore_header && t.variable > 0) {
                circuit_adjust(circuit, t.variable);
            }
        }
        t = next_token(&lexer, NONE);
    }
    
    // Parse quantifier prefix
    while (true) {
        if (t.type == LINE_END) {
            t = next_token(&lexer, LINE_END | FORALL | EXISTS | FREE | OUTPUT | VARIABLE);
            continue;
        }
        if (t.type == VARIABLE) {
            // end of quantification
            break;
        }
        if (t.type == OUTPUT) {
            next_token(&lexer, LPAREN);
            token output = next_token(&lexer, VARIABLE | LITERAL);
            next_token(&lexer, RPAREN);
            t = next_token(&lexer, LINE_END);
            circuit_set_output(circuit, output.literal);
            logging_debug("output %d\n", output.literal);
            continue;
        }
        Scope* scope;
        switch (t.type) {
            case FORALL:
                scope = circuit_init_scope(circuit, QUANT_FORALL);
                logging_debug("forall ");
                break;
                
            case EXISTS:
                scope = circuit_init_scope(circuit, QUANT_EXISTS);
                logging_debug("exists ");
                break;
                
            case FREE:
                scope = circuit_init_scope(circuit, QUANT_FREE);
                logging_debug("free ");
                break;
                
            default:
                assert(false);
                break;
        }
        assert(scope != NULL);
        next_token(&lexer, LPAREN);
        t = next_token(&lexer, VARIABLE | RPAREN);
        while (t.type & VARIABLE) {
            assert(t.type == VARIABLE);
            logging_debug("%d ", t.variable);
            circuit_new_var(circuit, scope, t.variable);
            t = next_token(&lexer, COMMA | RPAREN);
            if (t.type == RPAREN) {
                break;
            } else {
                assert(t.type == COMMA);
                t = next_token(&lexer, VARIABLE);
            }
        }
        t = next_token(&lexer, LINE_END);
        logging_debug("\n");
    }
    
    // parse the circuit part
    while (true) {
        if (t.type == LINE_END) {
            t = next_token(&lexer, LINE_END | FILE_END | SYMBOLTABLE | VARIABLE);
            continue;
        } else if (t.type & (FILE_END | SYMBOLTABLE)) {
            break;
        }
        assert(t.type == VARIABLE);
        
        var_t gate_var = t.variable;
        next_token(&lexer, ASSIGN);
        
        logging_debug("%d ", gate_var);
        
        t = next_token(&lexer, AND | OR | EXISTS | FORALL);
        if (t.type == AND || t.type == OR) {
            Gate* gate;
            if (t.type == AND) {
                gate = circuit_add_gate(circuit, gate_var, GATE_AND);
                logging_debug("and ");
            } else {
                assert(t.type == OR);
                gate = circuit_add_gate(circuit, gate_var, GATE_OR);
                logging_debug("or ");
            }
            next_token(&lexer, LPAREN);
            t = next_token(&lexer, VARIABLE | LITERAL | RPAREN);
            while (t.type & (VARIABLE | LITERAL)) {
                assert(gate != NULL);
                logging_debug("%d ", t.literal);
                circuit_add_to_gate(circuit, gate, t.literal);
                t = next_token(&lexer, COMMA | RPAREN);
                if (t.type == RPAREN) {
                    break;
                } else {
                    assert(t.type == COMMA);
                    t = next_token(&lexer, VARIABLE | LITERAL);
                }
            }
        } else {
            assert(t.type == EXISTS || t.type == FORALL);
            logging_debug("%s ", token_to_string(t.type));
            const quantifier_type qtype = t.type == EXISTS ? QUANT_EXISTS : QUANT_FORALL;
            ScopeNode* scope = circuit_new_scope_node(circuit, qtype, gate_var);
            next_token(&lexer, LPAREN);
            t = next_token(&lexer, VARIABLE | RPAREN);
            while (t.type & VARIABLE) {
                logging_debug("%d ", t.variable);
                circuit_new_var(circuit, scope->scope, t.variable);
                t = next_token(&lexer, COMMA | SEMICOLON);
                if (t.type == SEMICOLON) {
                    break;
                } else {
                    assert(t.type == COMMA);
                    t = next_token(&lexer, VARIABLE);
                }
            }
            t = next_token(&lexer, LITERAL | VARIABLE);
            circuit_set_scope_node(circuit, scope, t.literal);
            logging_debug("%d ", t.literal);
            next_token(&lexer, RPAREN);
        }
        t = next_token(&lexer, LINE_END);
        logging_debug("\n");
    }
    
    if (t.type == SYMBOLTABLE) {
        // Symboltable has the following form:
        // Header: #symboltable
        // Line: #id name, where id is id of node in circuit and name is unsigned integer
        // End: 2 empty lines
        t = next_token(&lexer, LINE_END);
        t = next_token(&lexer, LINE_END | FILE_END | LINE_COMMENT);
        while (t.type == LINE_COMMENT) {
            
            t = next_token(&lexer, VARIABLE);
            var_t id = t.variable;

            t = next_token(&lexer, VARIABLE);
            var_t name = t.variable;
            
            assert(id > 0);
            assert(name > 0);
            api_expect(id <= circuit->max_num, "id is larger than max num\n");
            api_expect(circuit->types[id] != 0, "id is not a valid node\n");
            node_shared* node = circuit->nodes[id];
            assert(node != NULL);
            node->orig_id = name;
            
            t = next_token(&lexer, LINE_END);
            t = next_token(&lexer, LINE_END | FILE_END | LINE_COMMENT);
        }
    }
    
    lexer_deinit(&lexer);
    return 0;
}
