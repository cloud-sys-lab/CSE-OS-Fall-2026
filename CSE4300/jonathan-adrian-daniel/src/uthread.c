#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "uthread_internal.h"

#ifndef sigev_notify_thread_id
#define sigev_notify_thread_id _sigev_un._tid
#endif

#define STACK_SIZE (64 * 1024)

static _Thread_local scheduler_t *current_sched;
static atomic_int active_cores;

static __attribute__((noinline)) scheduler_t *this_sched(void)
{
    return current_sched;
}

static void preempt_disable(void)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
}

static void preempt_enable(void)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static long now_real_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000L;
}

long now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000L;
}

static void wake_sleepers(scheduler_t *scheduler)
{
    long now = now_ms();
    int i = 0;

    while (i < scheduler->sleeping_count)
    {
        tcb_t *t = scheduler->sleeping[i];
        if (t->wake_at <= now)
        {
            scheduler->sleeping[i] = scheduler->sleeping[--scheduler->sleeping_count];
            t->state = READY;
            scheduler->policy->enqueue(scheduler->policy->instance, t);
        }
        else
        {
            i++;
        }
    }
}

static void charge_running(scheduler_t *scheduler)
{
    long used = now_us() - scheduler->running->dispatched_at;
    scheduler->running->remaining_us -= used;
    scheduler->stats[scheduler->running->tid].cpu_us += used;
    scheduler->running->level_used_us += used;
    if (scheduler->running->remaining_us < 0)
        scheduler->running->remaining_us = 0; // overran its estimate
}

static void arm_timer(scheduler_t *scheduler)
{
    if (!scheduler->timer_created)
    {
        struct sigevent sev;
        memset(&sev, 0, sizeof sev);
        sev.sigev_notify = SIGEV_THREAD_ID;
        sev.sigev_signo = SIGALRM;
        sev.sigev_value.sival_ptr = scheduler;
        sev.sigev_notify_thread_id = (pid_t)syscall(SYS_gettid);
        timer_create(CLOCK_MONOTONIC, &sev, &scheduler->timer);
        scheduler->timer_created = 1;
    }
    struct itimerspec its = {0};
    its.it_interval.tv_sec = scheduler->quantum_ms / 1000;
    its.it_interval.tv_nsec = (scheduler->quantum_ms % 1000) * 1000000L;
    its.it_value = its.it_interval;
    timer_settime(scheduler->timer, 0, &its, NULL);
}

static void dispatch(scheduler_t *scheduler, tcb_t *t)
{
    scheduler->running = t;
    t->state = RUNNING;
    t->dispatched_at = now_us();
    if (scheduler->stats[t->tid].first_run_us == 0)
        scheduler->stats[t->tid].first_run_us = now_real_us();
    if (t != scheduler->last_dispatched)
        scheduler->switch_count++;
    scheduler->last_dispatched = t;
    arm_timer(scheduler);
}

static void schedule(scheduler_t *scheduler)
{
    tcb_t *prev = scheduler->running;

    if (scheduler->policyAccessLock)
        pthread_mutex_lock(scheduler->policyAccessLock);

    if (prev->state == READY)
        scheduler->policy->enqueue(scheduler->policy->instance, prev);

    wake_sleepers(scheduler);
    tcb_t *next = scheduler->policy->pick_next(scheduler->policy->instance);

    while (next == NULL && scheduler->sleeping_count > 0)
    {
        sigset_t none;
        sigemptyset(&none);
        scheduler->running = NULL;
        if (scheduler->policyAccessLock)
            pthread_mutex_unlock(scheduler->policyAccessLock);
        sigsuspend(&none);
        if (scheduler->policyAccessLock)
            pthread_mutex_lock(scheduler->policyAccessLock);
        next = scheduler->policy->pick_next(scheduler->policy->instance);
    }

    if (next == NULL)
    {
        scheduler->running = NULL;
        if (scheduler->policyAccessLock)
            pthread_mutex_unlock(scheduler->policyAccessLock);
        swapcontext(&prev->ctx, &scheduler->main_ctx);
        return;
    }

    dispatch(scheduler, next);
    if (next == prev)
    {
        if (scheduler->policyAccessLock)
            pthread_mutex_unlock(scheduler->policyAccessLock);
        return;
    }

    swapcontext(&prev->ctx, &next->ctx);
    if (scheduler->policyAccessLock)
        pthread_mutex_unlock(scheduler->policyAccessLock);
}

static void thread_start(void)
{
    scheduler_t *scheduler = this_sched();
    if (scheduler->policyAccessLock)
        pthread_mutex_unlock(scheduler->policyAccessLock);
    preempt_enable();

    scheduler->running->fn(scheduler->running->arg);
    preempt_disable();
    scheduler = this_sched();
    charge_running(scheduler);
    scheduler->stats[scheduler->running->tid].finished_us = now_real_us();
    scheduler->running->state = DONE;
    schedule(scheduler);
}

static void on_timer(int sig, siginfo_t *info, void *ucontext)
{
    scheduler_t *scheduler = info->si_value.sival_ptr;

    if (scheduler->policyAccessLock)
        pthread_mutex_lock(scheduler->policyAccessLock);
    wake_sleepers(scheduler);
    int preempt = scheduler->running != NULL &&
                  scheduler->policy->should_preempt(scheduler->policy->instance, scheduler->running);
    if (preempt)
    {
        charge_running(scheduler);
        scheduler->running->state = READY;
    }
    if (scheduler->policyAccessLock)
        pthread_mutex_unlock(scheduler->policyAccessLock);

    if (preempt)
        schedule(scheduler);
}

static void install_timer_handler(void)
{
    struct sigaction sa = {0};
    sa.sa_sigaction = on_timer;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigaction(SIGALRM, &sa, NULL);
}

static void stop_timer(scheduler_t *scheduler)
{
    struct itimerspec its = {0};
    timer_settime(scheduler->timer, 0, &its, NULL);
}

void uthread_set_policy_lock(scheduler_t *scheduler, pthread_mutex_t *lock)
{
    scheduler->policyAccessLock = lock;
}

scheduler_t *uthread_init(policy_t which, int q)
{
    if (q <= 0)
        return NULL;

    scheduler_t *scheduler = calloc(1, sizeof *scheduler);
    if (!scheduler)
        return NULL;

    switch (which)
    {
    case POLICY_FCFS:
        scheduler->policy = &fcfs_policy;
        break;
    case POLICY_SJF:
        scheduler->policy = &sjf_policy;
        break;
    case POLICY_STCF:
        scheduler->policy = &stcf_policy;
        break;
    case POLICY_RR:
        scheduler->policy = &rr_policy;
        break;
    case POLICY_MLFQ:
        scheduler->policy = &mlfq_policy;
        break;
    default:
        free(scheduler);
        return NULL;
    }
    scheduler->quantum_ms = q;
    scheduler->policyAccessLock = NULL;
    if (scheduler->policy->init)
        scheduler->policy->instance = scheduler->policy->init(q);
    return scheduler;
}

int uthread_create(scheduler_t *scheduler, void (*fn)(void *), void *arg, const uthread_attr_t *attr)
{
    if (scheduler == NULL || scheduler->policy == NULL || scheduler->thread_count == MAX_THREADS)
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

    t->tid = scheduler->thread_count;
    t->fn = fn;
    t->arg = arg;
    t->expected_ms = attr ? attr->expected_ms : 0;
    t->remaining_us = (long)t->expected_ms * 1000;

    getcontext(&t->ctx);
    t->ctx.uc_stack.ss_sp = t->stack;
    t->ctx.uc_stack.ss_size = STACK_SIZE;
    t->ctx.uc_link = NULL;
    makecontext(&t->ctx, thread_start, 0);

    int delay = attr ? attr->start_delay_ms : 0;
    memset(&scheduler->stats[t->tid], 0, sizeof scheduler->stats[t->tid]);
    scheduler->stats[t->tid].arrived_us = now_real_us() + delay * 1000L;
    if (t->tid >= scheduler->stats_count)
        scheduler->stats_count = t->tid + 1;

    scheduler->threads[scheduler->thread_count++] = t;

    if (delay > 0)
    {
        t->state = BLOCKED;
        t->wake_at = now_ms() + delay;
        scheduler->sleeping[scheduler->sleeping_count++] = t;
        preempt_enable();
        return t->tid;
    }

    t->state = READY;
    if (scheduler->policyAccessLock)
        pthread_mutex_lock(scheduler->policyAccessLock);
    scheduler->policy->enqueue(scheduler->policy->instance, t);
    if (scheduler->policyAccessLock)
        pthread_mutex_unlock(scheduler->policyAccessLock);

    preempt_enable();
    return t->tid;
}

void uthread_yield(void)
{
    scheduler_t *scheduler = current_sched;
    if (scheduler == NULL || scheduler->running == NULL)
        return;

    preempt_disable();
    charge_running(scheduler);
    scheduler->running->state = READY;
    schedule(scheduler);
    preempt_enable();
}

void uthread_sleep(int ms)
{
    scheduler_t *scheduler = current_sched;
    if (scheduler == NULL || scheduler->running == NULL)
        return;

    preempt_disable();
    charge_running(scheduler);
    scheduler->running->state = BLOCKED;
    scheduler->running->wake_at = now_ms() + ms;
    scheduler->sleeping[scheduler->sleeping_count++] = scheduler->running;
    schedule(scheduler);
    preempt_enable();
}

void uthread_run(scheduler_t *scheduler)
{
    current_sched = scheduler;
    atomic_fetch_add(&active_cores, 1);
    preempt_disable();

    scheduler->switch_count = 0;
    scheduler->last_dispatched = NULL;
    install_timer_handler();

    if (scheduler->policyAccessLock)
        pthread_mutex_lock(scheduler->policyAccessLock);
    tcb_t *first = scheduler->policy->pick_next(scheduler->policy->instance);

    if (first == NULL && scheduler->sleeping_count > 0)
    {
        arm_timer(scheduler);
        while (first == NULL && scheduler->sleeping_count > 0)
        {
            sigset_t none;
            sigemptyset(&none);
            if (scheduler->policyAccessLock)
                pthread_mutex_unlock(scheduler->policyAccessLock);
            sigsuspend(&none);
            if (scheduler->policyAccessLock)
                pthread_mutex_lock(scheduler->policyAccessLock);
            wake_sleepers(scheduler);
            first = scheduler->policy->pick_next(scheduler->policy->instance);
        }
        if (first == NULL)
            stop_timer(scheduler);
    }

    if (first != NULL)
    {
        dispatch(scheduler, first);
        swapcontext(&(scheduler->main_ctx), &(scheduler->running->ctx));
        stop_timer(scheduler);
    }
    else if (scheduler->policyAccessLock)
    {
        pthread_mutex_unlock(scheduler->policyAccessLock);
    }

    if (scheduler->timer_created)
    {
        timer_delete(scheduler->timer);
        scheduler->timer_created = 0;
    }

    atomic_fetch_sub(&active_cores, 1);
    if (scheduler->policyAccessLock)
    {
        while (atomic_load(&active_cores) > 0)
            sched_yield();
    }

    for (int i = 0; i < scheduler->thread_count; i++)
    {
        free(scheduler->threads[i]->stack);
        free(scheduler->threads[i]);
    }
    scheduler->thread_count = 0;

    current_sched = NULL;
    preempt_enable();
}

int uthread_get_stats(scheduler_t *scheduler, int tid, uthread_stats_t *out)
{
    if (scheduler == NULL || tid < 0 || tid >= scheduler->stats_count || out == NULL)
        return -1;
    *out = scheduler->stats[tid];
    return 0;
}

long uthread_context_switches(scheduler_t *scheduler)
{
    return scheduler ? scheduler->switch_count : 0;
}
