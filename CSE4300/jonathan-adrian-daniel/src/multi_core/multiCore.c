#include "multi_core.h"
#include "uthread.h"
#include "uthread_internal.h"
#include <bits/pthreadtypes.h>
#include <pthread.h>                            //using pthreads to simulate each core on a cpu
#include <stdlib.h>
#include <threads.h>

#define MAX_CORES 16
pthread_mutex_t atomicParticipating = PTHREAD_MUTEX_INITIALIZER;
int totalCoresParticipating = 0;
scheduler_t *schedulers[MAX_CORES] = {NULL};                      //Initialize schedulers as a 1D array of dynamically allocated scheduler objects (1 To 1 with locks)
policy_m_t selectedPolicy = POLICY_UNDEFINED;

int initialize_multi_core(policy_m_t policy){
    
    switch (policy) {
        case POLICY_GLOBAL:
            selectedPolicy = policy;
            break;
        case POLICY_PARTITIONED:
            selectedPolicy = policy;
        default:
            return -1;
    }
    return 0;
}
int multi_core_destroy(){
    pthread_mutex_lock(&atomicParticipating);
    if (totalCoresParticipating != 0){
        pthread_mutex_unlock(&atomicParticipating);
        return -1;
    }
    pthread_mutex_unlock(&atomicParticipating);
    for (int i = 0; i < MAX_CORES; i++){
        if (schedulers[i] != NULL){
            uthread_destroy(schedulers[i]);
            schedulers[i] = NULL;
        }
    }
}
int coreRun(int id){
    pthread_mutex_lock(&atomicParticipating);
    if (1+totalCoresParticipating > MAX_CORES){
        return -1;
    }

    totalCoresParticipating++;
    //Starts the core's scheduler based on the selected policy.
    // if (selectedPolicy == POLICY_GLOBAL){
    //     pthread_mutex_unlock(&atomicParticipating);
    //     uthread_run(schedulers[0]);
    //     pthread_mutex_lock(&atomicParticipating);
    // }
    switch (selectedPolicy) {
        case POLICY_GLOBAL:
            pthread_mutex_unlock(&atomicParticipating);
            uthread_run(schedulers[0]);
            pthread_mutex_lock(&atomicParticipating);

            break;
        case POLICY_PARTITIONED:
            uthread_run(schedulers[id]);

            break;
        default:
            totalCoresParticipating--;
            pthread_mutex_unlock(&atomicParticipating);
            return -1;
    }
    totalCoresParticipating--;
    pthread_mutex_unlock(&atomicParticipating);
    return 0;
}

int setCorePolicy(int coreID, policy_t newPolicy, int quantum){
    //Changes a core's policy
    if (selectedPolicy == POLICY_GLOBAL && schedulers[0] == NULL){
        if (coreID > 0){
            //all cores share one scheduler in this instance, if trying to change any other core but the main core's scheduler, return an error.
            return -1;
        }
        schedulers[0] = uthread_init(newPolicy, quantum);
         
    }
    else{
        if(schedulers[coreID] != NULL)
            return -1;
        schedulers[coreID] = uthread_init(newPolicy, quantum);
    }
    return 0;
}

int policyAddProcess(int coreID, void (*proc)(void *arg), void* arg, const uthread_attr_t *attr){
    if (selectedPolicy == POLICY_GLOBAL && schedulers[0] != NULL){
        uthread_create(schedulers[0], proc, arg, attr);
    }
    return 0;
}