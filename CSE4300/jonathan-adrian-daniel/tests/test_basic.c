#include <stdio.h>
#include "uthread.h"

static void worker(void *arg)
{
    printf("%s\n", (char *)arg);
}

int main(void)
{
    scheduler_t *scheduler = uthread_init(POLICY_FCFS, 10);
    uthread_create(scheduler,worker, "A", NULL);
    uthread_create(scheduler, worker, "B", NULL);
    uthread_run(scheduler);
    printf("all threads finished\n");
    return 0;
}
