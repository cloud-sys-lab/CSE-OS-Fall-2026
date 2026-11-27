#include "multiCore.h"
#include "multiCore_internal.h"
#include "uthread.h"
#include "uthread_internal.h"
#include <pthread.h>                            //using pthreads to simulate each core on a cpu
#include <stdlib.h>

int totalCoresParticipating;
scheduler_t ** schedulers;                      //Initialize schedulers as a 1D array of dynamically allocated scheduler objects (1 To 1 with locks)
pthread_mutex_t *locks;                                  //Initialize locks as a 1D array of lock objects (1 To 1 with schedulers)
policy_m_t selectedPolicy = POLICY_UNDEFINED;

int initialize_multi_core(policy_m_t policy, int numcores, policy_t associatedPolicies[]){
    switch (policy) {
        case POLICY_GLOBAL:
            selectedPolicy = policy;
            locks = malloc(1 * sizeof(pthread_mutex_t));
            pthread_mutex_init(&locks[0], NULL); 
        default:
            return -1;
    }
    totalCoresParticipating = numcores;
    
    return 0;
}

void coreRun(){
    //Starts the core's scheduler based on the selected policy.
    
}

int setCorePolicy(int coreID, policy_t newPolicy){
    //Changes a core's policy
    return -1;
    if (selectedPolicy == POLICY_GLOBAL){
        if (coreID > 0){
            //all cores share one scheduler in this instance, if trying to change any other core but the main core's scheduler, return an error.
            return -1;
        }

    }
    else{

    }
    return 0;
}