#include <stdio.h>
#include "uthread.h"

static void worker(void *arg)
{
    for (int i = 0; i < 3; i++) {
        printf("%s%d\n", (char *)arg, i);
        uthread_yield();
    }
}

int main(void)
{
    uthread_init(POLICY_FCFS, 10);
    uthread_create(worker, "A", NULL);
    uthread_create(worker, "B", NULL);
    uthread_run();
    printf("all threads finished\n");
    return 0;
}
