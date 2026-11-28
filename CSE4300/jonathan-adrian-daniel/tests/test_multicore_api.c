#include "multi_core.h"
#include <stdio.h>
#include "uthread.h"
#include <assert.h>
#include <pthread.h>
pthread_mutex_t outputLock = PTHREAD_MUTEX_INITIALIZER;
char output[16];
int outputCnt = 0;
static char EXPECTEDBASIC[16] = {'A', 'D', 'B', 'C', '\n'};
static char EXPECTEDMULTI[16] = {'C', 'D', 'A', 'B', '\n'};
void* newThread(){
    assert(coreRun() == 0);
}
void workerThread(void* arg){
    char* string = (char*) arg;
    pthread_mutex_lock(&outputLock);
    output[outputCnt++] = *string;
    pthread_mutex_unlock(&outputLock);
}
int runBasicCall(){
    uthread_attr_t long_job  = { .expected_ms = 300 };
    uthread_attr_t mid_job   = { .expected_ms = 200 };
    uthread_attr_t short_job = { .expected_ms = 100 };
    uthread_attr_t tie_job   = { .expected_ms = 100 };
    assert(initialize_multi_core(POLICY_GLOBAL) == 0);
    assert(setCorePolicy(0, POLICY_SJF, 5) == 0);
    assert(policyAddProcess(0, workerThread, "C", &long_job) == 0);
    assert(policyAddProcess(0, workerThread, "B", &mid_job) == 0);
    assert(policyAddProcess(0, workerThread, "A", &short_job) == 0);
    assert(policyAddProcess(0, workerThread, "D", &tie_job) == 0);
    assert(coreRun() == 0);
    printf("Comparing generated order %s to expected thread order: %s", output, EXPECTEDBASIC);
    for (int i = 0; i < outputCnt; i++) {
        assert(output[i] == EXPECTEDBASIC[i]);
    }
    assert(multi_core_destroy() == 0);
    return 0;
}
int runMultiCore(){
    uthread_attr_t long_job  = { .expected_ms = 300 };
    uthread_attr_t short_job = { .expected_ms = 100 };
    assert(initialize_multi_core(POLICY_GLOBAL) == 0);
    assert(setCorePolicy(0, POLICY_STCF, 5) == 0);
    assert(policyAddProcess(0, workerThread, "A", &long_job) == 0);
    assert(policyAddProcess(0, workerThread, "B", &long_job) == 0);
    assert(policyAddProcess(0, workerThread, "C", &short_job) == 0);
    assert(policyAddProcess(0, workerThread, "D", &short_job) == 0);
    pthread_t threadA;
    pthread_t threadB;
    pthread_create(&threadA, NULL, newThread, NULL);
    pthread_create(&threadB, NULL, newThread, NULL);
    pthread_join(threadA, NULL);
    pthread_join(threadB, NULL);
    printf("Comparing generated order %s to expected thread order: %s", output, EXPECTEDMULTI);
    for (int i = 0; i < outputCnt; i++) {
        assert(output[i] == EXPECTEDMULTI[i]);
    }
    assert(multi_core_destroy() == 0);
    return 0;
}
int main(void)
{
    runBasicCall();
    outputCnt = 0;
    output[0] = '\n';
    runMultiCore();
    printf("PASS\n");
    return 0;
}