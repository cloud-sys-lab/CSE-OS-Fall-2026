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


int initialize_multi_core(policy_m_t policy, int numcores, policy_t associatedPolicies[]);
#endif
