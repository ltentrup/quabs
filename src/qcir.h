//
//  qcir.h
//  caqe-qcir
//
//  Created by Leander Tentrup on 01.07.15.
//  Copyright (c) 2015 Saarland University. All rights reserved.
//

#ifndef __caqe_qcir__qcir__
#define __caqe_qcir__qcir__

#include <stdio.h>

#include "circuit.h"

int circuit_from_qcir(Circuit*, FILE*, bool ignore_header);
int circuit_open_and_read_qcir_file(Circuit*, const char*, bool ignore_header);
int circuit_open_and_write_qcir_file(Circuit*, const char*);

#endif /* defined(__caqe_qcir__qcir__) */
