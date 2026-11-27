#define _GNU_SOURCE
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "uthread_internal.h"

#ifdef __linux__
#include <sys/syscall.h>
#ifndef sigev_notify_thread_id
#define sigev_notify_thread_id _sigev_un._tid
#endif
#endif

#define STACK_SIZE (64 * 1024)

static _Thread_local core_t *current_core;

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

long now_mono_us(void)
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
            scheduler->policy->enqueue(scheduler->policy_state, t);
        }
        else
        {
            i++;
        }
    }
}

static void charge_running(core_t *core)
{
    tcb_t *t = core->running;
    long used = now_us() - t->dispatched_at;
    t->remaining_us -= used;
    core->scheduler->stats[t->tid].cpu_us += used;
    t->level_used_us += used;
    if (t->remaining_us < 0)
        t->remaining_us = 0; // overran its estimate
}

static void arm_timer(core_t *core)
{
    int q = core->scheduler->quantum_ms;
#ifdef __linux__
    if (!core->timer_created)
    {
        struct sigevent sev;
        memset(&sev, 0, sizeof sev);
        sev.sigev_notify = SIGEV_THREAD_ID;
        sev.sigev_signo = SIGALRM;
        sev.sigev_notify_thread_id = (pid_t)syscall(SYS_gettid);
        if (timer_create(CLOCK_MONOTONIC, &sev, &core->timer) != 0)
            return;
        core->timer_created = 1;
    }
    struct itimerspec its = {0};
    its.it_interval.tv_sec = q / 1000;
    its.it_interval.tv_nsec = (q % 1000) * 1000000L;
    its.it_value = its.it_interval;
    timer_settime(core->timer, 0, &its, NULL);
#else
    // no per-thread timers outside Linux: fall back to the process-wide
    // ITIMER_REAL (best effort when more than one core is running)
    struct itimerval tv = {0};
    tv.it_interval.tv_sec = q / 1000;
    tv.it_interval.tv_usec = (q % 1000) * 1000;
    tv.it_value = tv.it_interval;
    setitimer(ITIMER_REAL, &tv, NULL);
    core->timer_created = 1;
#endif
}

static void destroy_timer(core_t *core)
{
    if (!core->timer_created)
        return;
#ifdef __linux__
    struct itimerspec its = {0};
    timer_settime(core->timer, 0, &its, NULL);
    timer_delete(core->timer);
#else
    struct itimerval tv = {0};
    setitimer(ITIMER_REAL, &tv, NULL);
#endif
    core->timer_created = 0;
}

static void dispatch(core_t *core, tcb_t *t)
{
    core->running = t;
    t->state = RUNNING;
    t->dispatched_at = now_us();
    arm_timer(core);
}

static void switch_to_core(tstate_t state)
{
    core_t *core = current_core;
    tcb_t *t = core->running;
    charge_running(core);
    t->state = state;
    swapcontext(&t->ctx, &core->sched_ctx);
}

static void thread_start(void)
{
    preempt_enable();
    tcb_t *self = current_core->running;
    self->fn(self->arg);
    preempt_disable();
    current_core->scheduler->stats[self->tid].finished_us = now_mono_us();
    switch_to_core(DONE);
}

static void on_timer(int sig, siginfo_t *info, void *ucontext)
{
    (void)sig;
    (void)info;
    (void)ucontext;

    core_t *core = current_core;
    if (core == NULL || core->running == NULL)
        return;

    scheduler_t *scheduler = core->scheduler;
    pthread_mutex_lock(&scheduler->lock);
    wake_sleepers(scheduler);
    int preempt = scheduler->policy->should_preempt(scheduler->policy_state, core->running);
    pthread_mutex_unlock(&scheduler->lock);

    if (preempt)
        switch_to_core(READY);
}

static void install_timer_handler(void)
{
    struct sigaction sa = {0};
    sa.sa_sigaction = on_timer;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigaction(SIGALRM, &sa, NULL);
}

static void free_threads(scheduler_t *scheduler)
{
    for (int i = 0; i < scheduler->thread_count; i++)
    {
        free(scheduler->threads[i]->stack);
        free(scheduler->threads[i]);
    }
    scheduler->thread_count = 0;
    scheduler->live_threads = 0;
    scheduler->sleeping_count = 0;
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
    scheduler->policy_state = scheduler->policy->init(q);
    if (scheduler->policy_state == NULL)
    {
        free(scheduler);
        return NULL;
    }
    pthread_mutex_init(&scheduler->lock, NULL);
    return scheduler;
}

void uthread_destroy(scheduler_t *scheduler)
{
    if (scheduler == NULL)
        return;
    pthread_mutex_lock(&scheduler->lock);
    int busy = scheduler->active_cores > 0;
    pthread_mutex_unlock(&scheduler->lock);
    if (busy)
        return;

    free_threads(scheduler);
    scheduler->policy->destroy(scheduler->policy_state);
    pthread_mutex_destroy(&scheduler->lock);
    free(scheduler);
}

int uthread_create(scheduler_t *scheduler, void (*fn)(void *), void *arg, const uthread_attr_t *attr)
{
    if (scheduler == NULL || fn == NULL)
        return -1;

    preempt_disable();

    tcb_t *t = calloc(1, sizeof *t);
    void *stack = t ? malloc(STACK_SIZE) : NULL;
    if (stack == NULL)
    {
        free(t);
        preempt_enable();
        return -1;
    }

    t->stack = stack;
    t->fn = fn;
    t->arg = arg;
    t->expected_ms = attr ? attr->expected_ms : 0;
    t->remaining_us = (long)t->expected_ms * 1000;
    t->state = READY;

    getcontext(&t->ctx);
    t->ctx.uc_stack.ss_sp = t->stack;
    t->ctx.uc_stack.ss_size = STACK_SIZE;
    t->ctx.uc_link = NULL;
    makecontext(&t->ctx, thread_start, 0);

    pthread_mutex_lock(&scheduler->lock);
    if (scheduler->thread_count == MAX_THREADS)
    {
        pthread_mutex_unlock(&scheduler->lock);
        free(t->stack);
        free(t);
        preempt_enable();
        return -1;
    }
    t->tid = scheduler->thread_count;
    scheduler->threads[scheduler->thread_count++] = t;
    scheduler->live_threads++;

    int delay = attr ? attr->start_delay_ms : 0;
    memset(&scheduler->stats[t->tid], 0, sizeof scheduler->stats[t->tid]);
    scheduler->stats[t->tid].arrived_us = now_mono_us() + delay * 1000L;
    if (t->tid >= scheduler->stats_count)
        scheduler->stats_count = t->tid + 1;

    if (delay > 0)
    {
        // delayed arrival: park it with the sleepers until its start time
        t->state = BLOCKED;
        t->wake_at = now_ms() + delay;
        scheduler->sleeping[scheduler->sleeping_count++] = t;
    }
    else
    {
        scheduler->policy->enqueue(scheduler->policy_state, t);
    }
    pthread_mutex_unlock(&scheduler->lock);

    preempt_enable();
    return t->tid;
}

void uthread_yield(void)
{
    preempt_disable();
    core_t *core = current_core;
    if (core != NULL && core->running != NULL)
        switch_to_core(READY);
    preempt_enable();
}

void uthread_sleep(int ms)
{
    preempt_disable();
    core_t *core = current_core;
    if (core != NULL && core->running != NULL)
    {
        core->running->wake_at = now_ms() + ms;
        switch_to_core(BLOCKED);
    }
    preempt_enable();
}

int uthread_core_id(void)
{
    core_t *core = current_core;
    return core ? core->id : -1;
}

void uthread_run(scheduler_t *scheduler)
{
    if (scheduler == NULL || current_core != NULL)
        return;

    sigset_t alrm, saved_mask, idle_mask;
    sigemptyset(&alrm);
    sigaddset(&alrm, SIGALRM);
    pthread_sigmask(SIG_BLOCK, &alrm, &saved_mask);
    idle_mask = saved_mask;
    sigdelset(&idle_mask, SIGALRM);

    install_timer_handler();

    core_t core;
    memset(&core, 0, sizeof core);
    core.scheduler = scheduler;

    pthread_mutex_lock(&scheduler->lock);
    if (scheduler->active_cores++ == 0)
        scheduler->switch_count = 0;
    core.id = scheduler->next_core_id++;
    current_core = &core;

    for (;;)
    {
        wake_sleepers(scheduler);
        tcb_t *t = scheduler->policy->pick_next(scheduler->policy_state);

        if (t != NULL)
        {
            if (scheduler->stats[t->tid].first_run_us == 0)
                scheduler->stats[t->tid].first_run_us = now_mono_us();
            if (t != core.last_dispatched)
                scheduler->switch_count++;
            core.last_dispatched = t;
            pthread_mutex_unlock(&scheduler->lock);
            dispatch(&core, t);
            swapcontext(&core.sched_ctx, &t->ctx);
            pthread_mutex_lock(&scheduler->lock);
            core.running = NULL;

            switch (t->state)
            {
            case READY:
                scheduler->policy->enqueue(scheduler->policy_state, t);
                break;
            case BLOCKED:
                scheduler->sleeping[scheduler->sleeping_count++] = t;
                break;
            case DONE:
                scheduler->live_threads--;
                break;
            default:
                break;
            }
            continue;
        }

        if (scheduler->live_threads == 0)
            break;

        if (!core.timer_created)
            arm_timer(&core);
        pthread_mutex_unlock(&scheduler->lock);
        sigsuspend(&idle_mask);
        pthread_mutex_lock(&scheduler->lock);
    }

    if (--scheduler->active_cores == 0)
        free_threads(scheduler);
    pthread_mutex_unlock(&scheduler->lock);

    destroy_timer(&core);
    current_core = NULL;

    sigset_t pending;
    int sig;
    while (sigpending(&pending) == 0 && sigismember(&pending, SIGALRM))
        sigwait(&alrm, &sig);
    pthread_sigmask(SIG_SETMASK, &saved_mask, NULL);
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
