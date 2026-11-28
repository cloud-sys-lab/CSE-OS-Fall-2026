#ifdef __linux__
#define _GNU_SOURCE
#include <sched.h>
#endif
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "uthread.h"
#include "multi_core.h"

#define MAX_JOBS 16
#define MAX_REPS 50
#define MAX_CORES 16   /* mirrors MAX_CORES in src/multi_core/multiCore.c */

typedef struct {
    const char *name;
    int arrive_ms;
    int cpu_ms;
    int estimate_ms;
    int bursts;
    int sleep_ms;
} job_t;

typedef struct {
    const char *name;
    const job_t *jobs;
    int njobs;
} workload_t;

typedef struct {
    double turnaround;
    double response;
    double wait;
    double max_turnaround;
    double switches;
    double wake_latency;
} result_t;

typedef struct {
    const job_t *job;
    long latency_us;
    int wakes;
    scheduler_t *sched;   /* scheduler the job was added to */
    int tid;              /* its thread id within that scheduler */
} job_run_t;

/* How a row was run: on one core with uthread_init/uthread_run directly, or
 * through multi_core.h with a shared (global) or per-core (partitioned)
 * scheduler. */
typedef enum { MODE_SINGLE, MODE_GLOBAL, MODE_PARTITIONED } mode_t_;

typedef struct {
    mode_t_ mode;
    int cores;
} config_t;

static const char *mode_names[] = { "single", "global", "partitioned" };

static const job_t convoy_jobs[] = {
    { "L",  0, 200, 200, 1, 0 },
    { "S1", 0,  10,  10, 1, 0 },
    { "S2", 0,  10,  10, 1, 0 },
    { "S3", 0,  10,  10, 1, 0 },
    { "S4", 0,  10,  10, 1, 0 },
};

static const job_t staggered_jobs[] = {
    { "L",    0, 200, 200, 1, 0 },
    { "S1",  40,  20,  20, 1, 0 },
    { "S2",  80,  20,  20, 1, 0 },
    { "S3", 120,  20,  20, 1, 0 },
};

static const job_t interactive_jobs[] = {
    { "B1", 0, 300, 300,  1,  0 },
    { "B2", 0, 300, 300,  1,  0 },
    { "I1", 0,   2,  20, 10, 20 },
    { "I2", 0,   2,  20, 10, 20 },
};

static const job_t bad_estimate_jobs[] = {
    { "L",  0, 200,  10, 1, 0 },
    { "S1", 0,  10, 200, 1, 0 },
    { "S2", 0,  10, 200, 1, 0 },
    { "S3", 0,  10, 200, 1, 0 },
    { "S4", 0,  10, 200, 1, 0 },
};

#define WORKLOAD(n, j) { n, j, (int) (sizeof j / sizeof j[0]) }

static const workload_t workloads[] = {
    WORKLOAD("convoy", convoy_jobs),
    WORKLOAD("staggered", staggered_jobs),
    WORKLOAD("interactive", interactive_jobs),
    WORKLOAD("bad-estimates", bad_estimate_jobs),
};

static const workload_t *interactive = &workloads[2];

static const struct {
    policy_t policy;
    const char *name;
} policies[] = {
    { POLICY_FCFS, "FCFS" },
    { POLICY_SJF,  "SJF"  },
    { POLICY_STCF, "STCF" },
    { POLICY_RR,   "RR"   },
    { POLICY_MLFQ, "MLFQ" },
};

#define NPOLICIES ((int) (sizeof policies / sizeof policies[0]))

static const int sweep_quanta[] = { 1, 5, 10, 50 };

static unsigned long iters_per_ms;
static int reps = 3;
static int csv;
static int run_single = 1;
static int run_multi = 1;
static const int multi_cores[] = { 2, 4 };

#ifdef __linux__
static cpu_set_t original_cpus;   /* affinity before pinning, for the cores */
#endif
static int core_cpu[MAX_CORES];   /* cpu each simulated core is pinned to, or -1 */

static long real_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000L;
}

static long cpu_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000L;
}

static int pin_to_current_cpu(void)
{
#ifdef __linux__
    int cpu = sched_getcpu();
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (cpu >= 0 && sched_setaffinity(0, sizeof set, &set) == 0)
        return cpu;
#endif
    return -1;
}

/* Picks a distinct cpu for each simulated core from the cpus the process was
 * allowed to use before pinning itself. Returns how many cores could get one. */
static int assign_core_cpus(void)
{
    int n = 0;
    for (int i = 0; i < MAX_CORES; i++)
        core_cpu[i] = -1;
#ifdef __linux__
    for (int cpu = 0; cpu < CPU_SETSIZE && n < MAX_CORES; cpu++)
        if (CPU_ISSET(cpu, &original_cpus))
            core_cpu[n++] = cpu;
#endif
    return n;
}

static int cmp_long(const void *a, const void *b)
{
    long x = *(const long *) a, y = *(const long *) b;
    return (x > y) - (x < y);
}

static void calibrate(void)
{
    const unsigned long n = 20000000UL;
    long samples[9];

    for (int i = 0; i < 9; i++) {
        volatile unsigned long count = 0;
        long start = cpu_us();
        for (unsigned long j = 0; j < n; j++)
            count++;
        (void) count;
        samples[i] = cpu_us() - start;
    }
    qsort(samples, 9, sizeof samples[0], cmp_long);
    iters_per_ms = n * 1000UL / (unsigned long) samples[4];
}

static void work(int ms)
{
    /* A local counter, not a shared global: with several cores running jobs
     * at once a shared one bounces between their caches and slows every
     * job down. */
    volatile unsigned long count = 0;
    unsigned long n = (unsigned long) ms * iters_per_ms;
    for (unsigned long i = 0; i < n; i++)
        count++;
    (void) count;
}

static void worker(void *arg)
{
    job_run_t *run = arg;
    const job_t *job = run->job;

    for (int i = 0; i < job->bursts; i++) {
        work(job->cpu_ms);
        if (i + 1 < job->bursts && job->sleep_ms > 0) {
            long wake_target = real_us() + job->sleep_ms * 1000L;
            uthread_sleep(job->sleep_ms);
            run->latency_us += real_us() - wake_target;
            run->wakes++;
        }
    }
}

/* Averages each job's stats from its scheduler. switches is the total
 * context switch count over every scheduler the jobs used. */
static result_t summarize(const workload_t *w, const job_run_t *runs, long switches)
{
    result_t r = {0};
    long latency = 0;
    int wakes = 0;

    for (int i = 0; i < w->njobs; i++) {
        const job_t *job = &w->jobs[i];
        uthread_stats_t st;
        if (uthread_get_stats(runs[i].sched, runs[i].tid, &st) != 0) {
            fprintf(stderr, "no stats for job %s\n", job->name);
            exit(1);
        }

        double turnaround = (st.finished_us - st.arrived_us) / 1000.0;
        double response = (st.first_run_us - st.arrived_us) / 1000.0;
        double slept = (double) job->sleep_ms * (job->bursts - 1);
        double wait = turnaround - st.cpu_us / 1000.0 - slept;

        r.turnaround += turnaround;
        r.response += response;
        r.wait += wait;
        if (turnaround > r.max_turnaround)
            r.max_turnaround = turnaround;

        latency += runs[i].latency_us;
        wakes += runs[i].wakes;
    }

    r.turnaround /= w->njobs;
    r.response /= w->njobs;
    r.wait /= w->njobs;
    r.switches = switches;
    r.wake_latency = wakes > 0 ? latency / 1000.0 / wakes : -1;
    return r;
}

static result_t run_once(const workload_t *w, policy_t p, int quantum)
{
    job_run_t runs[MAX_JOBS] = {0};

    scheduler_t *s = uthread_init(p, quantum);
    for (int i = 0; i < w->njobs; i++) {
        uthread_attr_t attr = {
            .expected_ms = w->jobs[i].estimate_ms,
            .start_delay_ms = w->jobs[i].arrive_ms,
        };
        runs[i].job = &w->jobs[i];
        runs[i].sched = s;
        runs[i].tid = uthread_create(s, worker, &runs[i], &attr);
    }
    uthread_run(s);

    result_t r = summarize(w, runs, uthread_context_switches(s));
    uthread_destroy(s);
    return r;
}

#ifdef __linux__
typedef struct {
    int core_id;
    pthread_barrier_t *barrier;
} core_arg_t;

static void *core_main(void *p)
{
    core_arg_t *c = p;
#ifdef __linux__
    cpu_set_t set;
    CPU_ZERO(&set);
    if (core_cpu[c->core_id] >= 0)
        CPU_SET(core_cpu[c->core_id], &set);
    else
        set = original_cpus;
    pthread_setaffinity_np(pthread_self(), sizeof set, &set);
#endif
    pthread_barrier_wait(c->barrier);   /* start every core together */
    if (coreRun(c->core_id) != 0)
        fprintf(stderr, "coreRun(%d) failed\n", c->core_id);
    return NULL;
}

/* Runs a workload through multi_core.h on cfg->cores simulated cores. Global
 * mode puts every job in one shared scheduler; partitioned mode gives every
 * core its own scheduler (all with the same policy) and deals the jobs out
 * round-robin, so job i runs only on core i % cores.
 *
 * policyAddProcess returns each job's tid and core_get_scheduler the
 * scheduler it went to, so stats are read the same way as on one core. They
 * have to be read before multi_core_destroy(), which frees the schedulers. */
static result_t run_once_multi(const workload_t *w, policy_t p, int quantum,
                               const config_t *cfg)
{
    job_run_t runs[MAX_JOBS] = {0};
    policy_m_t mp = cfg->mode == MODE_GLOBAL ? POLICY_GLOBAL : POLICY_PARTITIONED;

    if (initialize_multi_core(mp) != 0) {
        fprintf(stderr, "initialize_multi_core failed\n");
        exit(1);
    }
    /* global mode has one scheduler, set through core 0 */
    int nsched = cfg->mode == MODE_GLOBAL ? 1 : cfg->cores;
    for (int c = 0; c < nsched; c++) {
        if (setCorePolicy(c, p, quantum) != 0) {
            fprintf(stderr, "setCorePolicy(%d) failed\n", c);
            exit(1);
        }
    }
    for (int i = 0; i < w->njobs; i++) {
        uthread_attr_t attr = {
            .expected_ms = w->jobs[i].estimate_ms,
            .start_delay_ms = w->jobs[i].arrive_ms,
        };
        int core = cfg->mode == MODE_GLOBAL ? 0 : i % cfg->cores;
        runs[i].job = &w->jobs[i];
        runs[i].sched = core_get_scheduler(core);
        runs[i].tid = policyAddProcess(core, worker, &runs[i], &attr);
        if (runs[i].tid < 0 || runs[i].sched == NULL) {
            fprintf(stderr, "policyAddProcess(%d) failed\n", core);
            exit(1);
        }
    }

    pthread_t tids[MAX_CORES];
    core_arg_t args[MAX_CORES];
    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, NULL, (unsigned) cfg->cores);
    for (int c = 0; c < cfg->cores; c++) {
        args[c] = (core_arg_t){ c, &barrier };
        pthread_create(&tids[c], NULL, core_main, &args[c]);
    }
    for (int c = 0; c < cfg->cores; c++)
        pthread_join(tids[c], NULL);
    pthread_barrier_destroy(&barrier);

    /* In global mode every core id gives the same scheduler, so count it once. */
    long switches = 0;
    for (int c = 0; c < nsched; c++)
        switches += uthread_context_switches(core_get_scheduler(c));

    result_t r = summarize(w, runs, switches);
    multi_core_destroy();
    return r;
}
#endif

static void mean_sd(const double *v, int n, double *mean, double *sd)
{
    double sum = 0, sq = 0;
    for (int i = 0; i < n; i++)
        sum += v[i];
    *mean = sum / n;
    for (int i = 0; i < n; i++)
        sq += (v[i] - *mean) * (v[i] - *mean);
    *sd = n > 1 ? sqrt(sq / (n - 1)) : 0;
}

static void print_header(void)
{
    if (csv) {
        printf("workload,policy,quantum_ms,rep,avg_turnaround_ms,avg_response_ms,"
               "avg_wait_ms,max_turnaround_ms,context_switches,avg_wake_latency_ms,"
               "mode,cores\n");
        return;
    }
    printf("%-14s %-5s %-5s %4s  %16s  %16s  %9s  %9s  %8s  %9s\n",
           "workload", "pol", "cores", "q", "turnaround (ms)", "response (ms)",
           "wait", "max turn", "switches", "wake lat");
}

static const config_t single_core = { MODE_SINGLE, 1 };

/* Short label for the cores column: "1", or G/P plus the core count. */
static void config_label(const config_t *cfg, char *buf, size_t n)
{
    if (cfg->mode == MODE_SINGLE)
        snprintf(buf, n, "1");
    else
        snprintf(buf, n, "%c%d", cfg->mode == MODE_GLOBAL ? 'G' : 'P', cfg->cores);
}

static void bench(const workload_t *w, int pi, int quantum, const config_t *cfg)
{
    result_t results[MAX_REPS];
    double v[MAX_REPS] = {0};
    double t_mean, t_sd, r_mean, r_sd, wait, max_t, sw, lat, unused;

    for (int i = 0; i < reps; i++) {
        if (cfg->mode == MODE_SINGLE)
            results[i] = run_once(w, policies[pi].policy, quantum);
#ifdef __linux__
        else
            results[i] = run_once_multi(w, policies[pi].policy, quantum, cfg);
#endif
        if (csv) {
            result_t *r = &results[i];
            printf("%s,%s,%d,%d,%.2f,%.2f,%.2f,%.2f,%.0f,%.2f,%s,%d\n",
                   w->name, policies[pi].name, quantum, i + 1, r->turnaround,
                   r->response, r->wait, r->max_turnaround, r->switches,
                   r->wake_latency, mode_names[cfg->mode], cfg->cores);
        }
    }
    if (csv)
        return;

#define COLLECT(field, m, s) \
    for (int i = 0; i < reps; i++) v[i] = results[i].field; \
    mean_sd(v, reps, &m, &s)

    COLLECT(turnaround, t_mean, t_sd);
    COLLECT(response, r_mean, r_sd);
    COLLECT(wait, wait, unused);
    COLLECT(max_turnaround, max_t, unused);
    COLLECT(switches, sw, unused);
    COLLECT(wake_latency, lat, unused);
#undef COLLECT

    char latency[16] = "-";
    if (lat >= 0)
        snprintf(latency, sizeof latency, "%.1f", lat);
    char cores[8];
    config_label(cfg, cores, sizeof cores);

    printf("%-14s %-5s %-5s %4d  %8.1f ± %5.1f  %8.1f ± %5.1f  %9.1f  %9.1f  %8.0f  %9s\n",
           w->name, policies[pi].name, cores, quantum, t_mean, t_sd, r_mean, r_sd,
           wait, max_t, sw, latency);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0) {
            csv = 1;
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            reps = atoi(argv[++i]);
            if (reps < 1 || reps > MAX_REPS) {
                fprintf(stderr, "reps must be 1..%d\n", MAX_REPS);
                return 1;
            }
        } else if (strcmp(argv[i], "--single-only") == 0) {
            run_multi = 0;
        } else if (strcmp(argv[i], "--multi-only") == 0) {
            run_single = 0;
        } else {
            fprintf(stderr, "usage: %s [--csv] [-r reps] [--single-only | --multi-only]\n",
                    argv[0]);
            return 1;
        }
    }

    if (!run_single && !run_multi) {
        fprintf(stderr, "--single-only and --multi-only exclude each other\n");
        return 1;
    }
#ifdef __linux__
    if (sched_getaffinity(0, sizeof original_cpus, &original_cpus) != 0)
        CPU_ZERO(&original_cpus);
#else
    if (run_multi && !run_single) {
        fprintf(stderr, "multi-core needs per-thread timers (Linux only)\n");
        return 1;
    }
    run_multi = 0;
#endif
    int ncpus = assign_core_cpus();

    int cpu = pin_to_current_cpu();
    calibrate();
    if (!csv) {
        if (cpu >= 0)
            printf("pinned to cpu %d, ", cpu);
        else
            printf("not pinned to a cpu (results may be noisier), ");
        printf("calibrated %lu iterations/ms, %d reps per row\n\n", iters_per_ms, reps);
    }

    print_header();
    int nworkloads = (int) (sizeof workloads / sizeof workloads[0]);

    if (run_single) {
        for (int w = 0; w < nworkloads; w++) {
            for (int p = 0; p < NPOLICIES; p++)
                bench(&workloads[w], p, 10, &single_core);
            if (!csv)
                printf("\n");
        }

        if (!csv)
            printf("quantum sweep (interactive workload)\n");
        int nquanta = (int) (sizeof sweep_quanta / sizeof sweep_quanta[0]);
        for (int p = 0; p < NPOLICIES; p++) {
            if (policies[p].policy != POLICY_RR && policies[p].policy != POLICY_MLFQ)
                continue;
            for (int q = 0; q < nquanta; q++)
                bench(interactive, p, sweep_quanta[q], &single_core);
        }
    }

    if (run_multi) {
        if (!csv) {
            printf("%smulti-core via multi_core.h (G = global, P = partitioned, "
                   "jobs dealt round-robin to cores)\n", run_single ? "\n" : "");
            if (ncpus == 0)
                printf("could not read the allowed cpus, cores are not pinned\n");
        }
        int nmulti = (int) (sizeof multi_cores / sizeof multi_cores[0]);
        for (int w = 0; w < nworkloads; w++) {
            for (int m = 0; m < nmulti; m++) {
                if (ncpus > 0 && multi_cores[m] > ncpus) {
                    if (!csv)
                        printf("skipping %d cores: only %d cpus available\n",
                               multi_cores[m], ncpus);
                    continue;
                }
                config_t g = { MODE_GLOBAL, multi_cores[m] };
                config_t pt = { MODE_PARTITIONED, multi_cores[m] };
                for (int p = 0; p < NPOLICIES; p++)
                    bench(&workloads[w], p, 10, &g);
                for (int p = 0; p < NPOLICIES; p++)
                    bench(&workloads[w], p, 10, &pt);
            }
            if (!csv)
                printf("\n");
        }
    }
    return 0;
}
