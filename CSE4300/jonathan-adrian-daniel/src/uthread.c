#include <stddef.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
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

static void preempt_disable(void)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    sigprocmask(SIG_BLOCK, &set, NULL);
}

static void preempt_enable(void)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
}

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

long now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000L;
}

static void wake_sleepers(void)
{
    long now = now_ms();
    int i = 0;

    while (i < sleeping_count)
    {
        tcb_t *t = sleeping[i];
        if (t->wake_at <= now)
        {
            sleeping[i] = sleeping[--sleeping_count];
            t->state = READY;
            policy->enqueue(t);
        }
        else
        {
            i++;
        }
    }
}

static void charge_running(void)
{
    long used = now_us() - running->dispatched_at;
    running->remaining_us -= used;
    running->level_used_us += used;
    if (running->remaining_us < 0)
        running->remaining_us = 0; // overran its estimate
}

static void arm_timer(void)
{
    struct itimerval tv = {0};
    tv.it_interval.tv_sec = quantum_ms / 1000;
    tv.it_interval.tv_usec = (quantum_ms % 1000) * 1000;
    tv.it_value = tv.it_interval;
    setitimer(ITIMER_REAL, &tv, NULL);
}

static void dispatch(tcb_t *t)
{
    running = t;
    t->state = RUNNING;
    t->dispatched_at = now_us();
    arm_timer();
}

static void schedule(void)
{
    tcb_t *prev = running;

    wake_sleepers();
    tcb_t *next = policy->pick_next();

    while (next == NULL && sleeping_count > 0)
    {
        sigset_t none;
        sigemptyset(&none);
        running = NULL;
        sigsuspend(&none);
        next = policy->pick_next();
    }

    if (next == NULL)
    {
        running = NULL;
        swapcontext(&prev->ctx, &main_ctx);
        return;
    }

    dispatch(next);
    if (next == prev)
        return;

    swapcontext(&prev->ctx, &next->ctx);
}

static void thread_start(void)
{
    running->fn(running->arg);
    preempt_disable();
    running->state = DONE;
    schedule();
}

static void on_timer(int sig)
{
    wake_sleepers();

    if (running == NULL)
        return;

    if (policy->should_preempt(running))
    {
        charge_running();
        running->state = READY;
        policy->enqueue(running);
        schedule();
    }
}

static void install_timer_handler(void)
{
    struct sigaction sa = {0};
    sa.sa_handler = on_timer;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);
}

static void stop_timer(void)
{
    struct itimerval tv = {0};
    setitimer(ITIMER_REAL, &tv, NULL);
}

int uthread_init(policy_t which, int q)
{
    if (q <= 0)
        return -1;

    switch (which)
    {
    case POLICY_FCFS:
        policy = &fcfs_policy;
        break;
    case POLICY_SJF:
        policy = &sjf_policy;
        break;
    case POLICY_STCF:
        policy = &stcf_policy;
        break;
    case POLICY_RR:
        policy = &rr_policy;
        break;
    case POLICY_MLFQ:
        policy = &mlfq_policy;
        break;
    default:
        return -1;
    }
    quantum_ms = q;
    if (policy->init)
        policy->init(q);
    return 0;
}

int uthread_create(void (*fn)(void *), void *arg, const uthread_attr_t *attr)
{
    if (policy == NULL || thread_count == MAX_THREADS)
        return -1;

    preempt_disable();

    tcb_t *t = calloc(1, sizeof *t);
    if (t == NULL)
    {
        preempt_enable();
        return -1;
    }
    t->stack = malloc(STACK_SIZE);
    if (t->stack == NULL)
    {
        free(t);
        preempt_enable();
        return -1;
    }

    t->tid = thread_count;
    t->fn = fn;
    t->arg = arg;
    t->expected_ms = attr ? attr->expected_ms : 0;
    t->remaining_us = (long)t->expected_ms * 1000;

    getcontext(&t->ctx);
    t->ctx.uc_stack.ss_sp = t->stack;
    t->ctx.uc_stack.ss_size = STACK_SIZE;
    t->ctx.uc_link = NULL;
    sigdelset(&t->ctx.uc_sigmask, SIGALRM);
    makecontext(&t->ctx, thread_start, 0);

    t->state = READY;
    threads[thread_count++] = t;
    policy->enqueue(t);

    preempt_enable();
    return t->tid;
}

void uthread_yield(void)
{
    if (running == NULL)
        return;

    preempt_disable();
    charge_running();
    running->state = READY;
    policy->enqueue(running);
    schedule();
    preempt_enable();
}

void uthread_sleep(int ms)
{
    if (running == NULL)
        return;

    preempt_disable();
    charge_running();
    running->state = BLOCKED;
    running->wake_at = now_ms() + ms;
    sleeping[sleeping_count++] = running;
    schedule();
    preempt_enable();
}

void uthread_run(void)
{
    preempt_disable();

    tcb_t *first = policy->pick_next();
    if (first != NULL)
    {
        install_timer_handler();
        dispatch(first);
        swapcontext(&main_ctx, &running->ctx);
        stop_timer();
    }

    for (int i = 0; i < thread_count; i++)
    {
        free(threads[i]->stack);
        free(threads[i]);
    }
    thread_count = 0;

    preempt_enable();
}
