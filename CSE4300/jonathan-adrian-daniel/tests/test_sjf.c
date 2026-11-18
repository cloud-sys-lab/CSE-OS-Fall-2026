#include <stdio.h>
#include <string.h>
#include "uthread.h"

static char order[16];
static int pos;




static void worker(void *arg)
{
    order[pos++] = *(char *) arg;
}

int main(void)
{
    uthread_attr_t long_job  = { .expected_ms = 300 };
    uthread_attr_t mid_job   = { .expected_ms = 200 };
    uthread_attr_t short_job = { .expected_ms = 100 };
    uthread_attr_t tie_job   = { .expected_ms = 100 };

    uthread_init(POLICY_SJF, 10);
    uthread_create(worker, "C", &long_job);
    uthread_create(worker, "B", &mid_job);
    uthread_create(worker, "A", &short_job);
    uthread_create(worker, "D", &tie_job);
    uthread_run();

    order[pos] = '\0';
    printf("order: %s\n", order);
    if (strcmp(order, "ADBC") != 0) {
        printf("FAIL: expected ADBC\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
