// Simulated multi-core tests without going through multi core interface. Each pthread that calls uthread_run() is one simulated core.
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
#include "uthread.h"

#define CORES 4
#define PER_CORE 4
#define SHARED_THREADS 16
#define ROUNDS 20

static long cpu_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000L;
}

static void spin_us(long us)
{
    long start = cpu_us();
    while (cpu_us() - start < us)
        ;
}

typedef struct {
    pthread_t owner;
    atomic_int *done;
    atomic_int *wrong_core;
} own_arg_t;

static void own_worker(void *p)
{
    own_arg_t *a = p;
    for (int i = 0; i < 3; i++) {
        if (!pthread_equal(pthread_self(), a->owner))
            atomic_fetch_add(a->wrong_core, 1);
        spin_us(2000);
        uthread_yield();
    }
    if (!pthread_equal(pthread_self(), a->owner))
        atomic_fetch_add(a->wrong_core, 1);
    atomic_fetch_add(a->done, 1);
}

typedef struct {
    policy_t policy;
    atomic_int done;
    atomic_int wrong_core;
} own_core_t;

static void *own_core(void *p)
{
    own_core_t *c = p;
    own_arg_t args[PER_CORE];
    uthread_attr_t attr = { .expected_ms = 10 };

    scheduler_t *s = uthread_init(c->policy, 5);
    if (s == NULL)
        return NULL;
    for (int i = 0; i < PER_CORE; i++) {
        args[i] = (own_arg_t){ pthread_self(), &c->done, &c->wrong_core };
        uthread_create(s, own_worker, &args[i], &attr);
    }
    uthread_run(s);
    uthread_destroy(s);
    return NULL;
}

static int test_own(void)
{
    static const policy_t policies[CORES] = { POLICY_RR, POLICY_MLFQ, POLICY_FCFS, POLICY_STCF };
    own_core_t cores[CORES];
    pthread_t tids[CORES];

    for (int i = 0; i < CORES; i++) {
        cores[i].policy = policies[i];
        atomic_init(&cores[i].done, 0);
        atomic_init(&cores[i].wrong_core, 0);
        pthread_create(&tids[i], NULL, own_core, &cores[i]);
    }
    for (int i = 0; i < CORES; i++)
        pthread_join(tids[i], NULL);

    int ok = 1;
    for (int i = 0; i < CORES; i++) {
        int done = atomic_load(&cores[i].done);
        int wrong = atomic_load(&cores[i].wrong_core);
        if (done != PER_CORE || wrong != 0) {
            printf("  own core %d: %d/%d done, %d ran on the wrong pthread\n",
                   i, done, PER_CORE, wrong);
            ok = 0;
        }
    }
    return ok;
}

static scheduler_t *shared;
static atomic_int shared_done;
static atomic_int child_done;
static atomic_uint cores_seen;

static void note_core(void)
{
    int id = uthread_core_id();
    if (id >= 0 && id < 32)
        atomic_fetch_or(&cores_seen, 1u << id);
}

static void child(void *p)
{
    (void)p;
    note_core();
    spin_us(1000);
    atomic_fetch_add(&child_done, 1);
}

static void shared_worker(void *p)
{
    long i = (long)p;
    for (int round = 0; round < 3; round++) {
        note_core();
        spin_us(1500);
        if (i % 4 == 0)
            uthread_sleep(3);
        else if (i % 4 == 1)
            uthread_yield();
    }
    if (i == 0)
        uthread_create(shared, child, NULL, NULL);
    note_core();
    atomic_fetch_add(&shared_done, 1);
}

static void *shared_core(void *p)
{
    (void)p;
    uthread_run(shared);
    return NULL;
}

static int test_shared(policy_t policy, int *distinct_cores)
{
    pthread_t tids[CORES];

    atomic_store(&shared_done, 0);
    atomic_store(&child_done, 0);
    atomic_store(&cores_seen, 0);

    shared = uthread_init(policy, 5);
    if (shared == NULL)
        return 0;
    for (long i = 0; i < SHARED_THREADS; i++)
        uthread_create(shared, shared_worker, (void *)i, NULL);

    for (int i = 0; i < CORES; i++)
        pthread_create(&tids[i], NULL, shared_core, NULL);
    for (int i = 0; i < CORES; i++)
        pthread_join(tids[i], NULL);
    uthread_destroy(shared);
    shared = NULL;

    *distinct_cores = __builtin_popcount(atomic_load(&cores_seen));
    int done = atomic_load(&shared_done);
    int kids = atomic_load(&child_done);
    if (done != SHARED_THREADS || kids != 1) {
        printf("  shared: %d/%d workers done, %d/1 child done\n", done, SHARED_THREADS, kids);
        return 0;
    }
    return 1;
}

int main(void)
{
    static const policy_t shared_policies[] = { POLICY_RR, POLICY_MLFQ, POLICY_STCF, POLICY_FCFS };
    int failed = 0;
    int max_cores = 0;

    for (int r = 0; r < ROUNDS; r++) {
        if (!test_own()) {
            printf("FAIL: own schedulers, round %d\n", r);
            failed = 1;
        }
        int cores = 0;
        if (!test_shared(shared_policies[r % 4], &cores)) {
            printf("FAIL: shared scheduler, round %d\n", r);
            failed = 1;
        }
        if (cores > max_cores)
            max_cores = cores;
    }

    printf("own: %d rounds x %d cores x %d uthreads\n", ROUNDS, CORES, PER_CORE);
    printf("shared: %d rounds, up to %d of %d cores ran work\n", ROUNDS, max_cores, CORES);
    if (failed) {
        printf("FAIL\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
