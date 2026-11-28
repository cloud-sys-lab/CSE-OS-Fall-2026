#include "multi_core.h"
#include "uthread.h"
#include "uthread_internal.h"
#include <pthread.h>                            //using pthreads to simulate each core on a cpu
#include <stdlib.h>
#include <threads.h>

#define MAX_CORES 16
pthread_mutex_t atomicParticipating = PTHREAD_MUTEX_INITIALIZER;
int totalCoresParticipating = 0;
scheduler_t *schedulers[MAX_CORES] = {NULL};                      //Initialize schedulers as a 1D array of dynamically allocated scheduler objects (1 To 1 with locks)
policy_m_t selectedPolicy = POLICY_UNDEFINED;

int initialize_multi_core(policy_m_t policy){
    pthread_mutex_lock(&atomicParticipating);
    if (selectedPolicy != POLICY_UNDEFINED){
        pthread_mutex_unlock(&atomicParticipating);
        return -1;
    }

    switch (policy) {
        case POLICY_GLOBAL:
            selectedPolicy = policy;
            break;
        case POLICY_PARTITIONED:
            selectedPolicy = policy;
            break;

        default:
            pthread_mutex_unlock(&atomicParticipating);
            
            return -1;
    }
    pthread_mutex_unlock(&atomicParticipating);
    
    return 0;
}
int multi_core_destroy(){
    pthread_mutex_lock(&atomicParticipating);
    if (totalCoresParticipating != 0){
        pthread_mutex_unlock(&atomicParticipating);
        return -1;
    }
    for (int i = 0; i < MAX_CORES; i++){
        if (schedulers[i] != NULL){
            uthread_destroy(schedulers[i]);
            schedulers[i] = NULL;
        }
    }
    selectedPolicy = POLICY_UNDEFINED;
    pthread_mutex_unlock(&atomicParticipating);

    return 0;
}
int coreRun(int id){
    pthread_mutex_lock(&atomicParticipating);
    if ( id >= MAX_CORES || id < 0 || 1+totalCoresParticipating > MAX_CORES || (selectedPolicy != POLICY_GLOBAL && schedulers[id] == NULL) || (selectedPolicy == POLICY_GLOBAL && schedulers[0] == NULL)){
        pthread_mutex_unlock(&atomicParticipating);
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
            pthread_mutex_unlock(&atomicParticipating);
            uthread_run(schedulers[id]);
            pthread_mutex_lock(&atomicParticipating);

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
    if (coreID < 0)
        return -1;
    switch (selectedPolicy) {
        case POLICY_GLOBAL:
        if(schedulers[0] != NULL || coreID > 0)
            return -1;
        schedulers[0] = uthread_init(newPolicy, quantum);
        if (schedulers[0] == NULL)
            return -1;
        break;
        
        case POLICY_PARTITIONED:
            if(coreID >= MAX_CORES || schedulers[coreID] != NULL)
                return -1;
            schedulers[coreID] = uthread_init(newPolicy, quantum);
            if (schedulers[coreID] == NULL)
                return -1;
            break;

        default:
            return -1;
    }   
        return 0;
}

int policyAddProcess(int coreID, void (*proc)(void *arg), void* arg, const uthread_attr_t *attr){
    if (proc == NULL || coreID < 0 || coreID > MAX_CORES - 1)
        return -1;
    switch (selectedPolicy) {
        case POLICY_GLOBAL:
            if (schedulers[0] != NULL)
            {
                int status = uthread_create(schedulers[0], proc, arg, attr);
                if (status == -1){
                    return -1;
                }
            }
            else {
                return -1;
            }
            break;
        case POLICY_PARTITIONED:
            if (schedulers[coreID] != NULL)
            {
                int status = uthread_create(schedulers[coreID], proc, arg, attr);
                if (status == -1){
                    return -1;
                }
            }
            else {
                return -1;
            }
            break;
        default:
            return -1;
    }
    return 0;
}