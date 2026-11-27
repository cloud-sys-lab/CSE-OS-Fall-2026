#include <stdio.h>
#include <time.h>
#include "uthread.h"

static volatile int sleeper_done;
static volatile long worker_count;
static long slept_ms;
static long lone_slept_ms;

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void sleeper(void *arg)
{
    long start = now_ms();
    uthread_sleep(100);
    slept_ms = now_ms() - start;
    sleeper_done = 1;
}

static void worker(void *arg)
{
    while (!sleeper_done)
        worker_count++;
}

static void lone_sleeper(void *arg)
{
    long start = now_ms();
    uthread_sleep(50);
    lone_slept_ms = now_ms() - start;
}

int main(void)
{
    scheduler_t *scheduler = uthread_init(POLICY_RR, 10);
    uthread_create(scheduler, sleeper, NULL, NULL);
    uthread_create(scheduler, worker, NULL, NULL);
    uthread_run(scheduler);
    printf("sleeper asked for 100 ms, slept %ld ms\n", slept_ms);
    printf("worker looped %ld times while it slept\n", worker_count);

    scheduler_t *scheduler2 = uthread_init(POLICY_RR, 10);
    uthread_create(scheduler2, lone_sleeper, NULL, NULL);
    uthread_run(scheduler2);
    printf("lone sleeper asked for 50 ms, slept %ld ms\n", lone_slept_ms);

    printf("all threads finished\n");
    return 0;
}
