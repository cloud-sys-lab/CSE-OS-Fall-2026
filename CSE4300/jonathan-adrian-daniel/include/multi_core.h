#ifndef MULTITHREAD_H
#define MULTITHREAD_H

#ifndef UTHREAD_H
#include "uthread.h"
#endif

typedef enum {
    POLICY_UNDEFINED,
    POLICY_GLOBAL,
    POLICY_PARTITIONED
} policy_m_t;

int initialize_multi_core(policy_m_t policy);
int setCorePolicy(int coreID, policy_t newPolicy, int quantum);
int policyAddProcess(int coreID, void (*proc)(void *arg), void* arg, const uthread_attr_t *attr);
int coreRun(int coreID);
int multi_core_destroy();

scheduler_t* core_get_scheduler(int coreID);
#endif
