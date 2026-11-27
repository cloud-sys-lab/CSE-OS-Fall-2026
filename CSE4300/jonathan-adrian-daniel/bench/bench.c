#ifdef __linux__
#define _GNU_SOURCE
#include <sched.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "uthread.h"

#define MAX_JOBS 16
#define MAX_REPS 50

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
} job_run_t;

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

static volatile unsigned long sink;
static unsigned long iters_per_ms;
static int reps = 3;
static int csv;

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
        long start = cpu_us();
        for (unsigned long j = 0; j < n; j++)
            sink++;
        samples[i] = cpu_us() - start;
    }
    qsort(samples, 9, sizeof samples[0], cmp_long);
    iters_per_ms = n * 1000UL / (unsigned long) samples[4];
}

static void work(int ms)
{
    unsigned long n = (unsigned long) ms * iters_per_ms;
    for (unsigned long i = 0; i < n; i++)
        sink++;
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

static result_t run_once(const workload_t *w, policy_t p, int quantum)
{
    job_run_t runs[MAX_JOBS] = {0};
    int tids[MAX_JOBS];
    result_t r = {0};

    scheduler_t *s = uthread_init(p, quantum);
    for (int i = 0; i < w->njobs; i++) {
        uthread_attr_t attr = {
            .expected_ms = w->jobs[i].estimate_ms,
            .start_delay_ms = w->jobs[i].arrive_ms,
        };
        runs[i].job = &w->jobs[i];
        tids[i] = uthread_create(s, worker, &runs[i], &attr);
    }
    uthread_run(s);

    long latency = 0;
    int wakes = 0;

    for (int i = 0; i < w->njobs; i++) {
        const job_t *job = &w->jobs[i];
        uthread_stats_t st;
        uthread_get_stats(s, tids[i], &st);

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
    r.switches = uthread_context_switches(s);
    r.wake_latency = wakes > 0 ? latency / 1000.0 / wakes : -1;
    return r;
}

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
               "avg_wait_ms,max_turnaround_ms,context_switches,avg_wake_latency_ms\n");
        return;
    }
    printf("%-14s %-5s %4s  %16s  %16s  %9s  %9s  %8s  %9s\n",
           "workload", "pol", "q", "turnaround (ms)", "response (ms)",
           "wait", "max turn", "switches", "wake lat");
}

static void bench(const workload_t *w, int pi, int quantum)
{
    result_t results[MAX_REPS];
    double v[MAX_REPS] = {0};
    double t_mean, t_sd, r_mean, r_sd, wait, max_t, sw, lat, unused;

    for (int i = 0; i < reps; i++) {
        results[i] = run_once(w, policies[pi].policy, quantum);
        if (csv) {
            result_t *r = &results[i];
            printf("%s,%s,%d,%d,%.2f,%.2f,%.2f,%.2f,%.0f,%.2f\n",
                   w->name, policies[pi].name, quantum, i + 1, r->turnaround,
                   r->response, r->wait, r->max_turnaround, r->switches,
                   r->wake_latency);
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

    printf("%-14s %-5s %4d  %8.1f ± %5.1f  %8.1f ± %5.1f  %9.1f  %9.1f  %8.0f  %9s\n",
           w->name, policies[pi].name, quantum, t_mean, t_sd, r_mean, r_sd,
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
        } else {
            fprintf(stderr, "usage: %s [--csv] [-r reps]\n", argv[0]);
            return 1;
        }
    }

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
    for (int w = 0; w < nworkloads; w++) {
        for (int p = 0; p < NPOLICIES; p++)
            bench(&workloads[w], p, 10);
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
            bench(interactive, p, sweep_quanta[q]);
    }
    return 0;
}
