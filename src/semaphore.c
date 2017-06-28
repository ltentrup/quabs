//
//  semaphore.c
//  caqe-qcir
//
//  Created by Leander Tentrup on 21.02.16.
//  Copyright © 2016 Saarland University. All rights reserved.
//

#include "semaphore.h"

void semaphore_init(semaphore* sem, size_t value) {
    pthread_mutex_init(&sem->mutex, NULL);
    pthread_cond_init(&sem->condition, NULL);
    
    sem->value = value;
}


void semaphore_wait(semaphore* sem) {
    pthread_mutex_lock(&sem->mutex);
    while (sem->value == 0) {
        pthread_cond_wait(&sem->condition, &sem->mutex);
    }
    sem->value--;
    pthread_mutex_unlock(&sem->mutex);
}


void semaphore_post(semaphore* sem) {
    pthread_mutex_lock(&sem->mutex);
    sem->value++;
    pthread_cond_signal(&sem->condition);
    pthread_mutex_unlock(&sem->mutex);
}
