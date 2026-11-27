#include <stdio.h>
#include <string.h>
#include <time.h>
#include "uthread.h"

static char order[4];
static int pos;

static long cpu_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void spin(int ms)
{
    long start = cpu_ms();
    while (cpu_ms() - start < ms)
        ;
}

static void short_job(void *arg)
{
    (void) arg;
    spin(30);
    order[pos++] = 'S';
}

static void long_job(void *arg)
{
    uthread_attr_t short_attr = { .expected_ms = 50 };

    spin(100);
    uthread_create(arg, short_job, NULL, &short_attr);
    spin(100);
    order[pos++] = 'L';
}

static int run(policy_t p, int long_expected_ms, const char *expected, const char *label)
{
    uthread_attr_t long_attr = { .expected_ms = long_expected_ms };

    pos = 0;
    scheduler_t *scheduler = uthread_init(p, 10);
    uthread_create(scheduler, long_job, scheduler, &long_attr);
    uthread_run(scheduler);
    uthread_destroy(scheduler);
    order[pos] = '\0';

    printf("%s: order %s (expected %s)\n", label, order, expected);
    return strcmp(order, expected) != 0;
}

int main(void)
{
    int failed = 0;

    failed |= run(POLICY_STCF, 300, "SL", "STCF, long has 200ms left vs short's 50ms");
    failed |= run(POLICY_STCF, 120, "LS", "STCF, long has 20ms left vs short's 50ms");
    failed |= run(POLICY_SJF,  300, "LS", "SJF never preempts");

    if (failed) {
        printf("FAIL\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
