// Tests for the multi_core.h API on a simulated multi-core system.
//
// Each simulated core is a pthread that calls coreRun(coreID).
//
// Groups:
//   multicore    - API contract and lifecycle shared by every mode
//                  (init / setCorePolicy / policyAddProcess / coreRun / destroy)
//   global       - POLICY_GLOBAL: every core pulls from one shared scheduler
//   partitioned  - POLICY_PARTITIONED: each core owns its scheduler and its jobs
//
// Every test runs in its own forked child process with a watchdog, so a test
// that crashes, fails a check or deadlocks only marks itself and the rest
// still run with fresh global state. (uthread uses SIGALRM for preemption, so
// the watchdog lives in the parent instead of using alarm().)
//
// Usage: test_multicore_api [multicore|global|partitioned]...
//   With no arguments every group runs. Exit status is 0 only if every test
//   that ran passed.

// Expanded upon with AI to generate more edge tests than originally written.

#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "multi_core.h"
#include "uthread.h"

// Mirrors MAX_CORES in src/multi_core/multiCore.c.
#define TEST_MAX_CORES 16
#define TEST_TIMEOUT_MS 10000
#define MAX_JOBS 64
#define LOG_LEN 128

/* ------------------------------------------------------------------ */
/* Check helpers                                                       */
/* ------------------------------------------------------------------ */

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "      check failed: %s  (%s:%d)\n", #cond,     \
                    __FILE__, __LINE__);                                    \
            return 0;                                                       \
        }                                                                   \
    } while (0)

#define CHECK_EQ(actual, expected)                                          \
    do {                                                                    \
        long _a = (long)(actual), _e = (long)(expected);                    \
        if (_a != _e) {                                                     \
            fprintf(stderr, "      check failed: %s == %s (got %ld, "       \
                    "expected %ld)  (%s:%d)\n", #actual, #expected, _a, _e, \
                    __FILE__, __LINE__);                                    \
            return 0;                                                       \
        }                                                                   \
    } while (0)

/* ------------------------------------------------------------------ */
/* Timing                                                              */
/* ------------------------------------------------------------------ */

static long thread_cpu_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000L;
}

static long mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void spin_us(long us)
{
    long start = thread_cpu_us();
    while (thread_cpu_us() - start < us)
        ;
}

/* ------------------------------------------------------------------ */
/* Simulated cores                                                     */
/* ------------------------------------------------------------------ */

static pthread_t core_owner[TEST_MAX_CORES];   // pthread simulating each core
static int core_rc[TEST_MAX_CORES];            // coreRun() return per core
static pthread_barrier_t core_barrier;

typedef struct {
    int core_id;
} core_arg_t;

static void *core_main(void *p)
{
    core_arg_t *c = p;
    core_owner[c->core_id] = pthread_self();
    pthread_barrier_wait(&core_barrier);       // start every core together
    core_rc[c->core_id] = coreRun(c->core_id);
    return NULL;
}

// Runs coreRun(ids[i]) on n simulated cores at once and waits for them all.
static void run_cores(int n, const int *ids)
{
    pthread_t tids[TEST_MAX_CORES];
    core_arg_t args[TEST_MAX_CORES];

    pthread_barrier_init(&core_barrier, NULL, (unsigned)n);
    for (int i = 0; i < n; i++) {
        args[i].core_id = ids[i];
        core_rc[ids[i]] = 12345;               // sentinel: coreRun never returned
        pthread_create(&tids[i], NULL, core_main, &args[i]);
    }
    for (int i = 0; i < n; i++)
        pthread_join(tids[i], NULL);
    pthread_barrier_destroy(&core_barrier);
}

static void run_cores_0_to_n(int n)
{
    int ids[TEST_MAX_CORES];
    for (int i = 0; i < n; i++)
        ids[i] = i;
    run_cores(n, ids);
}

/* ------------------------------------------------------------------ */
/* Jobs                                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    char tag;            // letter written to the logs
    int core;            // core the job was added to
    long spin_us;        // CPU work per round
    int rounds;          // rounds of work, yielding between them
    // filled in by the job
    atomic_int runs;     // how many times the job body ran to completion
    int uthread_core;    // uthread_core_id() seen when it finished
    pthread_t ran_on;    // pthread it finished on
    atomic_int wrong_core;  // rounds that ran on a pthread other than the core's
} job_t;

static job_t jobs[MAX_JOBS];
static int njobs;

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static char start_log[LOG_LEN];   // tag when a job first runs
static int start_cnt;
static char done_log[LOG_LEN];    // tag when a job finishes
static int done_cnt;
static char core_log[TEST_MAX_CORES][LOG_LEN];   // finish order per core
static int core_log_cnt[TEST_MAX_CORES];

static void log_push(char *buf, int *cnt, char tag)
{
    if (*cnt < LOG_LEN - 1) {
        buf[(*cnt)++] = tag;
        buf[*cnt] = '\0';
    }
}

static void reset_state(void)
{
    memset(jobs, 0, sizeof jobs);
    njobs = 0;
    memset(start_log, 0, sizeof start_log);
    memset(done_log, 0, sizeof done_log);
    memset(core_log, 0, sizeof core_log);
    memset(core_log_cnt, 0, sizeof core_log_cnt);
    start_cnt = done_cnt = 0;
}

static job_t *new_job(char tag, int core, long spin, int rounds)
{
    job_t *j = &jobs[njobs++];
    j->tag = tag;
    j->core = core;
    j->spin_us = spin;
    j->rounds = rounds;
    atomic_init(&j->runs, 0);
    atomic_init(&j->wrong_core, 0);
    j->uthread_core = -1;
    return j;
}

static void job_fn(void *p)
{
    job_t *j = p;

    pthread_mutex_lock(&log_lock);
    log_push(start_log, &start_cnt, j->tag);
    pthread_mutex_unlock(&log_lock);

    for (int r = 0; r < j->rounds; r++) {
        if (!pthread_equal(pthread_self(), core_owner[j->core]))
            atomic_fetch_add(&j->wrong_core, 1);
        spin_us(j->spin_us);
        if (r + 1 < j->rounds)
            uthread_yield();
    }
    if (!pthread_equal(pthread_self(), core_owner[j->core]))
        atomic_fetch_add(&j->wrong_core, 1);

    j->uthread_core = uthread_core_id();
    j->ran_on = pthread_self();

    pthread_mutex_lock(&log_lock);
    log_push(done_log, &done_cnt, j->tag);
    if (j->core >= 0 && j->core < TEST_MAX_CORES)
        log_push(core_log[j->core], &core_log_cnt[j->core], j->tag);
    pthread_mutex_unlock(&log_lock);

    atomic_fetch_add(&j->runs, 1);
}

// Adds a job through the API. Returns policyAddProcess()'s result.
static int add_job(char tag, int core, int expected_ms, long spin, int rounds)
{
    job_t *j = new_job(tag, core, spin, rounds);
    uthread_attr_t attr = { .expected_ms = expected_ms };
    return policyAddProcess(core, job_fn, j, &attr);
}

static int all_jobs_ran_once(void)
{
    for (int i = 0; i < njobs; i++) {
        int runs = atomic_load(&jobs[i].runs);
        if (runs != 1) {
            fprintf(stderr, "      job '%c' ran %d times (expected 1)\n",
                    jobs[i].tag, runs);
            return 0;
        }
    }
    return 1;
}

static int jobs_stayed_on_their_cores(void)
{
    for (int i = 0; i < njobs; i++) {
        int wrong = atomic_load(&jobs[i].wrong_core);
        if (wrong != 0) {
            fprintf(stderr, "      job '%c' (core %d) ran %d time(s) on another "
                    "core's pthread\n", jobs[i].tag, jobs[i].core, wrong);
            return 0;
        }
    }
    return 1;
}

static int str_eq_report(const char *what, const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "      %s order \"%s\", expected \"%s\"\n", what, got, want);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Rendezvous jobs: prove two cores execute at the same time           */
/* ------------------------------------------------------------------ */

static atomic_int rv_arrived;
static atomic_int rv_saw_partner;

static void rendezvous_fn(void *p)
{
    (void)p;
    atomic_fetch_add(&rv_arrived, 1);
    long deadline = mono_ms() + 1500;
    // Busy-wait without yielding: the partner can only arrive if another core
    // is really running at the same time.
    while (atomic_load(&rv_arrived) < 2 && mono_ms() < deadline)
        ;
    if (atomic_load(&rv_arrived) >= 2)
        atomic_fetch_add(&rv_saw_partner, 1);
}

/* ------------------------------------------------------------------ */
/* Job that calls the API from inside a running core                   */
/* ------------------------------------------------------------------ */

static int destroy_rc_inside = 12345;

static void destroy_inside_fn(void *p)
{
    (void)p;
    destroy_rc_inside = multi_core_destroy();
}

/* ================================================================== */
/* Group: multicore (API contract and lifecycle)                       */
/* ================================================================== */

static int mc_init_accepts_global(void)
{
    CHECK_EQ(initialize_multi_core(POLICY_GLOBAL), 0);
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int mc_init_accepts_partitioned(void)
{
    CHECK_EQ(initialize_multi_core(POLICY_PARTITIONED), 0);
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int mc_init_rejects_invalid_policy(void)
{
    CHECK_EQ(initialize_multi_core(POLICY_UNDEFINED), -1);
    CHECK_EQ(initialize_multi_core((policy_m_t)42), -1);
    return 1;
}

static int mc_destroy_without_init(void)
{
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int mc_corerun_before_init_fails(void)
{
    CHECK_EQ(coreRun(0), -1);
    return 1;
}

static int mc_set_policy_rejects_bad_core_global(void)
{
    CHECK_EQ(initialize_multi_core(POLICY_GLOBAL), 0);
    CHECK_EQ(setCorePolicy(-1, POLICY_FCFS, 5), -1);
    CHECK_EQ(setCorePolicy(TEST_MAX_CORES, POLICY_FCFS, 5), -1);
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int mc_set_policy_rejects_bad_core_partitioned(void)
{
    CHECK_EQ(initialize_multi_core(POLICY_PARTITIONED), 0);
    CHECK_EQ(setCorePolicy(-1, POLICY_FCFS, 5), -1);
    CHECK_EQ(setCorePolicy(TEST_MAX_CORES, POLICY_FCFS, 5), -1);
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int mc_add_rejects_null_fn(void)
{
    uthread_attr_t attr = { .expected_ms = 10 };
    CHECK_EQ(initialize_multi_core(POLICY_GLOBAL), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_FCFS, 5), 0);
    CHECK_EQ(policyAddProcess(0, NULL, NULL, &attr), -1);
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int mc_add_before_set_policy_fails(void)
{
    CHECK_EQ(initialize_multi_core(POLICY_GLOBAL), 0);
    CHECK_EQ(add_job('A', 0, 10, 0, 1), -1);   // no scheduler exists yet
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int mc_destroy_while_running_fails(void)
{
    uthread_attr_t attr = { .expected_ms = 10 };
    CHECK_EQ(initialize_multi_core(POLICY_GLOBAL), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_FCFS, 5), 0);
    CHECK_EQ(policyAddProcess(0, destroy_inside_fn, NULL, &attr), 0);
    CHECK_EQ(coreRun(0), 0);
    CHECK_EQ(destroy_rc_inside, -1);           // a core was still running
    CHECK_EQ(multi_core_destroy(), 0);         // nothing running any more
    return 1;
}

static int mc_reinit_after_destroy(void)
{
    // Round 1: global
    CHECK_EQ(initialize_multi_core(POLICY_GLOBAL), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_FCFS, 5), 0);
    CHECK_EQ(add_job('A', 0, 10, 0, 1), 0);
    CHECK_EQ(coreRun(0), 0);
    CHECK_EQ(multi_core_destroy(), 0);
    CHECK(all_jobs_ran_once());

    // Round 2: global again; the scheduler slot must be free again
    reset_state();
    CHECK_EQ(initialize_multi_core(POLICY_GLOBAL), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_FCFS, 5), 0);
    CHECK_EQ(add_job('B', 0, 10, 0, 1), 0);
    CHECK_EQ(coreRun(0), 0);
    CHECK_EQ(multi_core_destroy(), 0);
    CHECK(all_jobs_ran_once());
    CHECK(str_eq_report("finish", done_log, "B"));
    return 1;
}

static int mc_switch_global_to_partitioned(void)
{
    CHECK_EQ(initialize_multi_core(POLICY_GLOBAL), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_FCFS, 5), 0);
    CHECK_EQ(add_job('A', 0, 10, 0, 1), 0);
    CHECK_EQ(coreRun(0), 0);
    CHECK_EQ(multi_core_destroy(), 0);

    reset_state();
    CHECK_EQ(initialize_multi_core(POLICY_PARTITIONED), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_FCFS, 5), 0);
    CHECK_EQ(setCorePolicy(1, POLICY_FCFS, 5), 0);  // only legal in partitioned
    CHECK_EQ(add_job('B', 0, 10, 0, 1), 0);
    CHECK_EQ(add_job('C', 1, 10, 0, 1), 0);
    run_cores_0_to_n(2);
    CHECK_EQ(core_rc[0], 0);
    CHECK_EQ(core_rc[1], 0);
    CHECK(all_jobs_ran_once());
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

/* ================================================================== */
/* Group: global                                                       */
/* ================================================================== */

static int gl_single_core_sjf_order(void)
{
    CHECK_EQ(initialize_multi_core(POLICY_GLOBAL), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_SJF, 5), 0);
    CHECK_EQ(add_job('C', 0, 300, 0, 1), 0);
    CHECK_EQ(add_job('B', 0, 200, 0, 1), 0);
    CHECK_EQ(add_job('A', 0, 100, 0, 1), 0);
    CHECK_EQ(add_job('D', 0, 100, 0, 1), 0);   // ties with A, added after it
    CHECK_EQ(coreRun(0), 0);
    CHECK(all_jobs_ran_once());
    CHECK(str_eq_report("finish", done_log, "ADBC"));
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int gl_single_core_fcfs_order(void)
{
    CHECK_EQ(initialize_multi_core(POLICY_GLOBAL), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_FCFS, 5), 0);
    CHECK_EQ(add_job('A', 0, 300, 0, 1), 0);
    CHECK_EQ(add_job('B', 0, 100, 0, 1), 0);
    CHECK_EQ(add_job('C', 0, 200, 0, 1), 0);
    CHECK_EQ(coreRun(0), 0);
    CHECK(all_jobs_ran_once());
    CHECK(str_eq_report("finish", done_log, "ABC"));
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int gl_only_core0_sets_policy(void)
{
    CHECK_EQ(initialize_multi_core(POLICY_GLOBAL), 0);
    CHECK_EQ(setCorePolicy(1, POLICY_RR, 5), -1);  // shared scheduler lives on core 0
    CHECK_EQ(setCorePolicy(0, POLICY_RR, 5), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_FCFS, 5), -1); // already set
    CHECK_EQ(setCorePolicy(3, POLICY_FCFS, 5), -1);
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int gl_add_through_any_core_id(void)
{
    // In global mode the core id passed to policyAddProcess does not matter:
    // every job lands in the one shared queue.
    CHECK_EQ(initialize_multi_core(POLICY_GLOBAL), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_FCFS, 5), 0);
    for (int c = 0; c < 4; c++)
        CHECK_EQ(add_job((char)('A' + c), c, 10, 0, 1), 0);
    CHECK_EQ(coreRun(0), 0);                   // only one core runs
    CHECK(all_jobs_ran_once());
    CHECK(str_eq_report("finish", done_log, "ABCD"));
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int gl_four_cores_run_every_job_once(void)
{
    CHECK_EQ(initialize_multi_core(POLICY_GLOBAL), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_RR, 5), 0);
    for (int i = 0; i < 16; i++)
        CHECK_EQ(add_job((char)('a' + i), 0, 10, 1000, 3), 0);
    run_cores_0_to_n(4);
    for (int c = 0; c < 4; c++)
        CHECK_EQ(core_rc[c], 0);
    CHECK(all_jobs_ran_once());
    CHECK_EQ(done_cnt, 16);
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int gl_work_spreads_across_cores(void)
{
    CHECK_EQ(initialize_multi_core(POLICY_GLOBAL), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_RR, 5), 0);
    for (int i = 0; i < 16; i++)
        CHECK_EQ(add_job((char)('a' + i), 0, 10, 2000, 3), 0);
    run_cores_0_to_n(4);
    CHECK(all_jobs_ran_once());

    unsigned ids_seen = 0;
    int distinct_pthreads = 0;
    pthread_t seen[MAX_JOBS];
    for (int i = 0; i < njobs; i++) {
        if (jobs[i].uthread_core >= 0 && jobs[i].uthread_core < 32)
            ids_seen |= 1u << jobs[i].uthread_core;
        int dup = 0;
        for (int k = 0; k < distinct_pthreads; k++)
            if (pthread_equal(seen[k], jobs[i].ran_on))
                dup = 1;
        if (!dup)
            seen[distinct_pthreads++] = jobs[i].ran_on;
    }
    if (__builtin_popcount(ids_seen) < 2 || distinct_pthreads < 2)
        fprintf(stderr, "      jobs finished on %d core id(s), %d pthread(s)\n",
                __builtin_popcount(ids_seen), distinct_pthreads);
    CHECK(__builtin_popcount(ids_seen) >= 2);
    CHECK(distinct_pthreads >= 2);
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int gl_two_cores_stcf_shortest_first(void)
{
    CHECK_EQ(initialize_multi_core(POLICY_GLOBAL), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_STCF, 5), 0);
    CHECK_EQ(add_job('A', 0, 300, 20000, 1), 0);
    CHECK_EQ(add_job('B', 0, 300, 20000, 1), 0);
    CHECK_EQ(add_job('C', 0, 100, 2000, 1), 0);
    CHECK_EQ(add_job('D', 0, 100, 2000, 1), 0);
    run_cores_0_to_n(2);
    CHECK_EQ(core_rc[0], 0);
    CHECK_EQ(core_rc[1], 0);
    CHECK(all_jobs_ran_once());
    // Whichever core gets there first, the two short jobs must be picked first.
    int short_first = (start_log[0] == 'C' || start_log[0] == 'D') &&
                      (start_log[1] == 'C' || start_log[1] == 'D');
    if (!short_first)
        fprintf(stderr, "      start order \"%s\", expected C and D first\n", start_log);
    CHECK(short_first);
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int gl_cores_run_in_parallel(void)
{
    uthread_attr_t attr = { .expected_ms = 10 };
    atomic_store(&rv_arrived, 0);
    atomic_store(&rv_saw_partner, 0);
    CHECK_EQ(initialize_multi_core(POLICY_GLOBAL), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_FCFS, 5), 0);
    CHECK_EQ(policyAddProcess(0, rendezvous_fn, NULL, &attr), 0);
    CHECK_EQ(policyAddProcess(0, rendezvous_fn, NULL, &attr), 0);
    run_cores_0_to_n(2);
    CHECK_EQ(core_rc[0], 0);
    CHECK_EQ(core_rc[1], 0);
    CHECK_EQ(atomic_load(&rv_saw_partner), 2);
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

/* ================================================================== */
/* Group: partitioned                                                  */
/* ================================================================== */

static int pt_each_core_sets_own_policy(void)
{
    static const policy_t pol[4] = { POLICY_RR, POLICY_MLFQ, POLICY_FCFS, POLICY_STCF };
    CHECK_EQ(initialize_multi_core(POLICY_PARTITIONED), 0);
    for (int c = 0; c < 4; c++)
        CHECK_EQ(setCorePolicy(c, pol[c], 5), 0);
    CHECK_EQ(setCorePolicy(2, POLICY_SJF, 5), -1);  // already set
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int pt_add_to_unconfigured_core_fails(void)
{
    CHECK_EQ(initialize_multi_core(POLICY_PARTITIONED), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_FCFS, 5), 0);
    CHECK_EQ(add_job('A', 1, 10, 0, 1), -1);   // core 1 has no scheduler
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int pt_corerun_unconfigured_core_fails(void)
{
    CHECK_EQ(initialize_multi_core(POLICY_PARTITIONED), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_FCFS, 5), 0);
    CHECK_EQ(coreRun(1), -1);                  // core 1 has no scheduler
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int pt_single_core_runs_its_jobs(void)
{
    CHECK_EQ(initialize_multi_core(POLICY_PARTITIONED), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_FCFS, 5), 0);
    CHECK_EQ(add_job('A', 0, 10, 0, 1), 0);
    CHECK_EQ(add_job('B', 0, 10, 0, 1), 0);
    run_cores_0_to_n(1);
    CHECK_EQ(core_rc[0], 0);
    CHECK(all_jobs_ran_once());
    CHECK(str_eq_report("core 0 finish", core_log[0], "AB"));
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int pt_jobs_stay_on_their_core(void)
{
    static const policy_t pol[4] = { POLICY_RR, POLICY_MLFQ, POLICY_FCFS, POLICY_STCF };
    CHECK_EQ(initialize_multi_core(POLICY_PARTITIONED), 0);
    for (int c = 0; c < 4; c++)
        CHECK_EQ(setCorePolicy(c, pol[c], 5), 0);
    for (int c = 0; c < 4; c++)
        for (int k = 0; k < 4; k++)
            CHECK_EQ(add_job((char)('a' + c * 4 + k), c, 10, 1500, 3), 0);
    run_cores_0_to_n(4);
    for (int c = 0; c < 4; c++)
        CHECK_EQ(core_rc[c], 0);
    CHECK(all_jobs_ran_once());
    CHECK(jobs_stayed_on_their_cores());
    for (int c = 0; c < 4; c++)
        CHECK_EQ(core_log_cnt[c], 4);
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int pt_per_core_policy_order(void)
{
    // Core 0 runs SJF, core 1 runs FCFS, at the same time; each must keep
    // its own ordering.
    CHECK_EQ(initialize_multi_core(POLICY_PARTITIONED), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_SJF, 5), 0);
    CHECK_EQ(setCorePolicy(1, POLICY_FCFS, 5), 0);
    CHECK_EQ(add_job('C', 0, 300, 0, 1), 0);
    CHECK_EQ(add_job('B', 0, 200, 0, 1), 0);
    CHECK_EQ(add_job('A', 0, 100, 0, 1), 0);
    CHECK_EQ(add_job('X', 1, 300, 0, 1), 0);
    CHECK_EQ(add_job('Y', 1, 100, 0, 1), 0);
    CHECK_EQ(add_job('Z', 1, 200, 0, 1), 0);
    run_cores_0_to_n(2);
    CHECK_EQ(core_rc[0], 0);
    CHECK_EQ(core_rc[1], 0);
    CHECK(all_jobs_ran_once());
    CHECK(jobs_stayed_on_their_cores());
    CHECK(str_eq_report("core 0 (SJF) finish", core_log[0], "ABC"));
    CHECK(str_eq_report("core 1 (FCFS) finish", core_log[1], "XYZ"));
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int pt_no_work_stealing(void)
{
    // Core 1 is idle; it must not take any of core 0's jobs.
    CHECK_EQ(initialize_multi_core(POLICY_PARTITIONED), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_RR, 5), 0);
    CHECK_EQ(setCorePolicy(1, POLICY_RR, 5), 0);
    for (int k = 0; k < 6; k++)
        CHECK_EQ(add_job((char)('a' + k), 0, 10, 2000, 3), 0);
    run_cores_0_to_n(2);
    CHECK_EQ(core_rc[0], 0);
    CHECK_EQ(core_rc[1], 0);                   // idle core returns cleanly
    CHECK(all_jobs_ran_once());
    CHECK(jobs_stayed_on_their_cores());
    CHECK_EQ(core_log_cnt[0], 6);
    CHECK_EQ(core_log_cnt[1], 0);
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int pt_nonzero_core_ids(void)
{
    // Partitioned cores need not start at 0 or be contiguous.
    int ids[2] = { 3, 7 };
    CHECK_EQ(initialize_multi_core(POLICY_PARTITIONED), 0);
    CHECK_EQ(setCorePolicy(3, POLICY_FCFS, 5), 0);
    CHECK_EQ(setCorePolicy(7, POLICY_RR, 5), 0);
    CHECK_EQ(add_job('A', 3, 10, 500, 2), 0);
    CHECK_EQ(add_job('B', 7, 10, 500, 2), 0);
    run_cores(2, ids);
    CHECK_EQ(core_rc[3], 0);
    CHECK_EQ(core_rc[7], 0);
    CHECK(all_jobs_ran_once());
    CHECK(jobs_stayed_on_their_cores());
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

static int pt_cores_run_in_parallel(void)
{
    uthread_attr_t attr = { .expected_ms = 10 };
    atomic_store(&rv_arrived, 0);
    atomic_store(&rv_saw_partner, 0);
    CHECK_EQ(initialize_multi_core(POLICY_PARTITIONED), 0);
    CHECK_EQ(setCorePolicy(0, POLICY_FCFS, 5), 0);
    CHECK_EQ(setCorePolicy(1, POLICY_FCFS, 5), 0);
    CHECK_EQ(policyAddProcess(0, rendezvous_fn, NULL, &attr), 0);
    CHECK_EQ(policyAddProcess(1, rendezvous_fn, NULL, &attr), 0);
    run_cores_0_to_n(2);
    CHECK_EQ(core_rc[0], 0);
    CHECK_EQ(core_rc[1], 0);
    CHECK_EQ(atomic_load(&rv_saw_partner), 2);
    CHECK_EQ(multi_core_destroy(), 0);
    return 1;
}

/* ================================================================== */
/* Runner                                                              */
/* ================================================================== */

typedef struct {
    const char *group;
    const char *name;
    int (*fn)(void);
} test_t;

#define T(group, fn) { group, #fn, fn }

static const test_t tests[] = {
    T("multicore", mc_init_accepts_global),
    T("multicore", mc_init_accepts_partitioned),
    T("multicore", mc_init_rejects_invalid_policy),
    T("multicore", mc_destroy_without_init),
    T("multicore", mc_corerun_before_init_fails),
    T("multicore", mc_set_policy_rejects_bad_core_global),
    T("multicore", mc_set_policy_rejects_bad_core_partitioned),
    T("multicore", mc_add_rejects_null_fn),
    T("multicore", mc_add_before_set_policy_fails),
    T("multicore", mc_destroy_while_running_fails),
    T("multicore", mc_reinit_after_destroy),
    T("multicore", mc_switch_global_to_partitioned),

    T("global", gl_single_core_sjf_order),
    T("global", gl_single_core_fcfs_order),
    T("global", gl_only_core0_sets_policy),
    T("global", gl_add_through_any_core_id),
    T("global", gl_four_cores_run_every_job_once),
    T("global", gl_work_spreads_across_cores),
    T("global", gl_two_cores_stcf_shortest_first),
    T("global", gl_cores_run_in_parallel),

    T("partitioned", pt_each_core_sets_own_policy),
    T("partitioned", pt_add_to_unconfigured_core_fails),
    T("partitioned", pt_corerun_unconfigured_core_fails),
    T("partitioned", pt_single_core_runs_its_jobs),
    T("partitioned", pt_jobs_stay_on_their_core),
    T("partitioned", pt_per_core_policy_order),
    T("partitioned", pt_no_work_stealing),
    T("partitioned", pt_nonzero_core_ids),
    T("partitioned", pt_cores_run_in_parallel),
};
#define NTESTS ((int)(sizeof tests / sizeof tests[0]))

static const char *groups[] = { "multicore", "global", "partitioned" };
#define NGROUPS 3

enum { R_PASS, R_FAIL, R_CRASH, R_TIMEOUT };
static const char *result_name[] = { "PASS", "FAIL", "CRASH", "TIMEOUT" };

static int run_isolated(const test_t *t)
{
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return R_CRASH;
    }
    if (pid == 0) {
        reset_state();
        int ok = t->fn();
        fflush(stdout);
        fflush(stderr);
        _exit(ok ? 0 : 1);
    }

    long deadline = mono_ms() + TEST_TIMEOUT_MS;
    for (;;) {
        int status;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            if (WIFEXITED(status))
                return WEXITSTATUS(status) == 0 ? R_PASS : R_FAIL;
            if (WIFSIGNALED(status))
                fprintf(stderr, "      killed by signal %d\n", WTERMSIG(status));
            return R_CRASH;
        }
        if (mono_ms() > deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            fprintf(stderr, "      no result after %d ms (deadlock?)\n", TEST_TIMEOUT_MS);
            return R_TIMEOUT;
        }
        struct timespec ts = { 0, 5 * 1000000L };
        nanosleep(&ts, NULL);
    }
}

static int group_selected(const char *group, int argc, char **argv)
{
    if (argc <= 1)
        return 1;
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], group) == 0)
            return 1;
    return 0;
}

int main(int argc, char **argv)
{
#ifndef __linux__
    printf("SKIP: multi-core needs per-thread timers (Linux only)\n");
    return 0;
#endif
    for (int i = 1; i < argc; i++) {
        int known = 0;
        for (int g = 0; g < NGROUPS; g++)
            if (strcmp(argv[i], groups[g]) == 0)
                known = 1;
        if (!known) {
            fprintf(stderr, "unknown group '%s' (use multicore, global, partitioned)\n",
                    argv[i]);
            return 2;
        }
    }

    int passed[NGROUPS] = { 0 }, total[NGROUPS] = { 0 };
    int any_failed = 0;

    for (int g = 0; g < NGROUPS; g++) {
        if (!group_selected(groups[g], argc, argv))
            continue;
        printf("== %s\n", groups[g]);
        for (int i = 0; i < NTESTS; i++) {
            if (strcmp(tests[i].group, groups[g]) != 0)
                continue;
            int r = run_isolated(&tests[i]);
            printf("  %-8s %s\n", result_name[r], tests[i].name);
            fflush(stdout);
            total[g]++;
            if (r == R_PASS)
                passed[g]++;
            else
                any_failed = 1;
        }
    }

    printf("\nsummary:\n");
    for (int g = 0; g < NGROUPS; g++)
        if (total[g] > 0)
            printf("  %-12s %d/%d passed\n", groups[g], passed[g], total[g]);
    printf(any_failed ? "FAIL\n" : "PASS\n");
    return any_failed ? 1 : 0;
}
