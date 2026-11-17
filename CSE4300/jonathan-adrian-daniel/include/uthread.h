#ifndef UTHREAD_H
#define UTHREAD_H

typedef enum {
    POLICY_FCFS,
    POLICY_SJF,
    POLICY_STCF,
    POLICY_RR,
    POLICY_MLFQ
} policy_t;

typedef struct {
    int expected_ms;
} uthread_attr_t;

int  uthread_init(policy_t policy, int quantum_ms);

int  uthread_create(void (*fn)(void *), void *arg, const uthread_attr_t *attr);

void uthread_yield(void);

void uthread_sleep(int ms);

void uthread_run(void);

#endif
