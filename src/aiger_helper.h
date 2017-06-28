//
//  aiger_helper.h
//  caqe-qcir
//
//  Created by Leander Tentrup on 06.04.16.
//  Copyright © 2016 Saarland University. All rights reserved.
//

#ifndef aiger_helper_h
#define aiger_helper_h

#include "aiger.h"
#include "queue.h"

void encode_and_as(aiger*, unsigned* next_lit, int_queue*, unsigned dest_aiger_lit);
void encode_or_as(aiger*, unsigned* next_lit, int_queue*, unsigned dest_aiger_lit);

unsigned encode_and(aiger*, unsigned* next_lit, int_queue*);
unsigned encode_or(aiger*, unsigned* next_lit, int_queue*);

#endif /* aiger_helper_h */
