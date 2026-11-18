#include <stdio.h>
#include "uthread.h"

static void worker(void *arg)
{
    printf("%s\n", (char *)arg);
}

int main(void)
{
    uthread_init(POLICY_FCFS, 10);
    uthread_create(worker, "A", NULL);
    uthread_create(worker, "B", NULL);
    uthread_run();
    return 0;
}
