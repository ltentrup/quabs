//
//  circuit_print.h
//  caqe-qcir
//
//  Created by Leander Tentrup on 04.02.16.
//  Copyright © 2016 Saarland University. All rights reserved.
//

#ifndef circuit_print_h
#define circuit_print_h

#include "circuit.h"

void circuit_print_qcir(Circuit*);
void circuit_write_qcir(Circuit*, FILE*);
void circuit_write_qcir_with_symboltable(Circuit*, FILE*);
void circuit_print_dot(Circuit*);
void circuit_print_qdimacs(Circuit*);

#endif /* circuit_print_h */
