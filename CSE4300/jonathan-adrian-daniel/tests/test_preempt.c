#include <stdio.h>
#include "uthread.h"

static volatile int a_started;
static volatile int b_started;
static volatile long a_count;
static volatile long b_count;

static void thread_a(void *arg)
{
    a_started = 1;
    while (!b_started)
        a_count++;
}

static void thread_b(void *arg)
{
    b_started = 1;
    while (!a_started)
        b_count++;
}

int main(void)
{
    uthread_init(POLICY_RR, 10);
    uthread_create(thread_a, NULL, NULL);
    uthread_create(thread_b, NULL, NULL);
    uthread_run();
    printf("A spun %ld times before B got the CPU\n", a_count);
    printf("all threads finished\n");
    return 0;
}
