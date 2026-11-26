#include <stdio.h>
#include <time.h>
#include "uthread.h"

static volatile int b_active;
static volatile int a_ran_during_b;
static char order[4];
static int pos;

static long cpu_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void spin(int ms, int is_hog)
{
    long start = cpu_ms();
    while (cpu_ms() - start < ms) {
        if (is_hog && b_active)
            a_ran_during_b = 1;
    }
}

static void short_job(void *arg)
{
    (void) arg;
    b_active = 1;
    spin(15, 0);
    b_active = 0;
    order[pos++] = 'B';
}

static void hog(void *arg)
{
    (void) arg;
    spin(60, 1);
    uthread_create(short_job, NULL, NULL);
    spin(60, 1);
    order[pos++] = 'A';
}

static int run(policy_t p)
{
    b_active = 0;
    a_ran_during_b = 0;
    pos = 0;

    uthread_init(p, 10);
    uthread_create(hog, NULL, NULL);
    uthread_run();
    order[pos] = '\0';
    return a_ran_during_b;
}

int main(void)
{
    int rr_interleaved = run(POLICY_RR);
    printf("RR:   order %s, hog ran while short job active: %s\n",
           order, rr_interleaved ? "yes" : "no");

    int mlfq_interleaved = run(POLICY_MLFQ);
    printf("MLFQ: order %s, hog ran while short job active: %s\n",
           order, mlfq_interleaved ? "yes" : "no");

    if (mlfq_interleaved || order[0] != 'B' || order[1] != 'A') {
        printf("FAIL: demoted hog should not run until the new job finishes\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
