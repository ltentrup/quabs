//
//  semaphore.h
//  caqe-qcir
//
//  Created by Leander Tentrup on 21.02.16.
//  Copyright © 2016 Saarland University. All rights reserved.
//

#ifndef semaphore_h
#define semaphore_h

#include <stdio.h>

#include <pthread.h>

struct semaphore {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    
    size_t value;
};

typedef struct semaphore semaphore;

void semaphore_init(semaphore*, size_t);
void semaphore_wait(semaphore*);
void semaphore_post(semaphore*);

#endif /* semaphore_h */
