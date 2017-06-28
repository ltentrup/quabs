//
//  circuit_check.h
//  caqe-qcir
//
//  Created by Leander Tentrup on 04.02.16.
//  Copyright © 2016 Saarland University. All rights reserved.
//

#ifndef circuit_check_h
#define circuit_check_h

#include "circuit.h"

bool circuit_check(Circuit*);
bool circuit_check_occurrences(Circuit*);
bool circuit_check_all_nodes_defined(Circuit* circuit);

#endif /* circuit_check_h */
