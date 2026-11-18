#include <stddef.h>
#include <stdlib.h>
#include "uthread_internal.h"

#define STACK_SIZE (64 * 1024)

static sched_policy_t *policy;
static tcb_t *running;
static int quantum_ms;
static ucontext_t main_ctx;

static tcb_t *threads[MAX_THREADS];
static int thread_count;

static tcb_t *sleeping[MAX_THREADS];
static int sleeping_count;

static void schedule(void)
{
    tcb_t *prev = running;
    tcb_t *next = policy->pick_next();

    if (next == NULL) {
        running = NULL;
        swapcontext(&prev->ctx, &main_ctx);
        return;
    }

    running = next;
    next->state = RUNNING;
    swapcontext(&prev->ctx, &next->ctx);
}

static void thread_start(void)
{
    running->fn(running->arg);
    running->state = DONE;
    schedule();
}

static void on_timer(int sig)
{
}

int uthread_init(policy_t which, int q)
{
    switch (which) {
    case POLICY_FCFS: policy = &fcfs_policy; break;
    case POLICY_SJF:  policy = &sjf_policy;  break;
    case POLICY_STCF: policy = &stcf_policy; break;
    case POLICY_RR:   policy = &rr_policy;   break;
    case POLICY_MLFQ: policy = &mlfq_policy; break;
    default: return -1;
    }
    quantum_ms = q;
    return 0;
}

int uthread_create(void (*fn)(void *), void *arg, const uthread_attr_t *attr)
{
    if (policy == NULL || thread_count == MAX_THREADS)
        return -1;

    tcb_t *t = calloc(1, sizeof *t);
    if (t == NULL)
        return -1;
    t->stack = malloc(STACK_SIZE);
    if (t->stack == NULL) {
        free(t);
        return -1;
    }

    t->tid = thread_count;
    t->fn = fn;
    t->arg = arg;
    t->expected_ms = attr ? attr->expected_ms : 0;

    getcontext(&t->ctx);
    t->ctx.uc_stack.ss_sp = t->stack;
    t->ctx.uc_stack.ss_size = STACK_SIZE;
    t->ctx.uc_link = NULL;
    makecontext(&t->ctx, thread_start, 0);

    t->state = READY;
    threads[thread_count++] = t;
    policy->enqueue(t);
    return t->tid;
}

void uthread_yield(void)
{
}

void uthread_sleep(int ms)
{
}

void uthread_run(void)
{
    running = policy->pick_next();
    if (running != NULL) {
        running->state = RUNNING;
        swapcontext(&main_ctx, &running->ctx);
    }

    for (int i = 0; i < thread_count; i++) {
        free(threads[i]->stack);
        free(threads[i]);
    }
    thread_count = 0;
}
