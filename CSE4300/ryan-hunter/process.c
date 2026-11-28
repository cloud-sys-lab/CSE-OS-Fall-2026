#define _POSIX_C_SOURCE 200809L
#include "process.h"
#include "system_info.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    char name[PROC_NAME_MAX];
    char state;
    unsigned long long ticks;
    unsigned long long start;
    long rss_pages;
} RawStat;

typedef struct {
    pid_t pid;
    unsigned long long ticks;
    unsigned long long start;
} PrevSample;

static PrevSample *previous;
static int previous_count;
static double previous_time;

static double monotonic_seconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* comm (field 2) can contain spaces and ')', so find its final closing ')'. */
static int read_stat(pid_t pid, RawStat *r) {
    char path[64], buf[4096];
    if (pid <= 0 || !r) return -1;
    snprintf(path, sizeof path, "/proc/%ld/stat", (long)pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    if (n == 0) return -1;
    buf[n] = '\0';
    char *lp = strchr(buf, '('), *rp = strrchr(buf, ')');
    if (!lp || !rp || rp < lp || rp[1] != ' ') return -1;
    memset(r, 0, sizeof *r);
    size_t length = (size_t)(rp - lp - 1);
    if (length >= sizeof r->name) length = sizeof r->name - 1;
    memcpy(r->name, lp + 1, length);
    /* Never let a process name inject terminal escape/control characters. */
    for (size_t i = 0; i < length; ++i)
        if (!isprint((unsigned char)r->name[i])) r->name[i] = '?';

    char *save = NULL, *token = strtok_r(rp + 2, " \t\n", &save);
    unsigned long long user = 0, system = 0;
    int seen = 0;
    for (int field = 3; token && field <= 24; ++field) {
        char *end = NULL;
        errno = 0;
        if (field == 3) r->state = token[0];
        else if (field == 14 || field == 15 || field == 22) {
            unsigned long long value = strtoull(token, &end, 10);
            if (errno || end == token || *end || token[0] == '-') return -1;
            if (field == 14) user = value;
            if (field == 15) system = value;
            if (field == 22) r->start = value;
            ++seen;
        } else if (field == 24) {
            r->rss_pages = strtol(token, &end, 10);
            if (errno || end == token || *end || r->rss_pages < 0) return -1;
            ++seen;
        }
        token = strtok_r(NULL, " \t\n", &save);
    }
    if (seen != 4 || ULLONG_MAX - user < system) return -1;
    r->ticks = user + system;
    return 0;
}

static void fill_process(Process *p, pid_t pid, const RawStat *r, long total) {
    memset(p, 0, sizeof *p);
    p->pid = pid;
    memcpy(p->name, r->name, sizeof p->name);
    p->state = r->state;
    p->start_time_ticks = r->start;
    long page_kb = sysconf(_SC_PAGESIZE) / 1024;
    if (page_kb > 0 && r->rss_pages <= LONG_MAX / page_kb)
        p->mem_kb = r->rss_pages * page_kb;
    p->mem_percent = total > 0 ? 100.0 * (double)p->mem_kb / (double)total : 0.0;
}

const char *process_state_name(char state) {
    switch (state) {
        case 'R': return "Running";
        case 'S': return "Sleeping";
        case 'D': return "Disk wait";
        case 'T': return "Stopped";
        case 't': return "Traced";
        case 'Z': return "Zombie";
        case 'X': case 'x': return "Dead";
        case 'I': return "Idle";
        case 'P': return "Parked";
        default: return "Unknown";
    }
}

int get_process_info(pid_t pid, Process *p) {
    RawStat r;
    if (!p || read_stat(pid, &r) != 0) return -1;
    fill_process(p, pid, &r, get_total_memory_kb());
    return 0;
}

double get_process_memory_usage(pid_t pid) {
    Process p;
    return get_process_info(pid, &p) == 0 ? p.mem_percent : -1.0;
}

double get_process_cpu_usage(pid_t pid, int sample_ms) {
    RawStat a, b;
    long hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0 || read_stat(pid, &a) != 0) return -1.0;
    if (sample_ms <= 0) sample_ms = 100;
    double start = monotonic_seconds();
    struct timespec ts = {sample_ms / 1000, (long)(sample_ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
    double elapsed = monotonic_seconds() - start;
    if (read_stat(pid, &b) != 0 || b.start != a.start ||
        b.ticks < a.ticks || elapsed <= 0.0) return -1.0;
    return 100.0 * (double)(b.ticks - a.ticks) / (double)hz / elapsed;
}

static int compare_sample(const void *pa, const void *pb) {
    const PrevSample *a = pa, *b = pb;
    return (a->pid > b->pid) - (a->pid < b->pid);
}

int get_process_list(Process *out, int max) {
    if (!out || max <= 0) return -1;
    DIR *dir = opendir("/proc");
    if (!dir) return -1;
    PrevSample *next = calloc((size_t)max, sizeof *next);
    if (!next) { closedir(dir); return -1; }
    double now = monotonic_seconds(), elapsed = now - previous_time;
    long hz = sysconf(_SC_CLK_TCK), total_memory = get_total_memory_kb();
    int count = 0;
    struct dirent *entry;
    while (count < max && (entry = readdir(dir)) != NULL) {
        char *end;
        if (!isdigit((unsigned char)entry->d_name[0])) continue;
        errno = 0;
        long value = strtol(entry->d_name, &end, 10);
        if (errno || *end || value <= 0 || value > INT_MAX) continue;
        pid_t pid = (pid_t)value;
        RawStat raw;
        if (read_stat(pid, &raw) != 0) continue; /* Normal exit/access race. */
        fill_process(&out[count], pid, &raw, total_memory);
        PrevSample key = {.pid = pid};
        const PrevSample *old = previous_count > 0 ?
            bsearch(&key, previous, (size_t)previous_count, sizeof key, compare_sample) : NULL;
        if (old && old->start == raw.start && raw.ticks >= old->ticks &&
            previous_time > 0.0 && elapsed > 0.0 && hz > 0)
            out[count].cpu_percent = 100.0 * (double)(raw.ticks - old->ticks) /
                                     (double)hz / elapsed;
        next[count] = (PrevSample){pid, raw.ticks, raw.start};
        ++count;
    }
    closedir(dir);
    qsort(next, (size_t)count, sizeof *next, compare_sample);
    free(previous);
    previous = next;
    previous_count = count;
    previous_time = now;
    return count;
}

int terminate_process(pid_t pid, int force) {
    if (pid <= 1 || pid == getpid()) return -EINVAL;
    if (force != 0 && force != 1) return -EINVAL;
    return kill(pid, force ? SIGKILL : SIGTERM) == 0 ? 0 : -errno;
}

void cleanup_process_cache(void) {
    free(previous);
    previous = NULL;
    previous_count = 0;
    previous_time = 0.0;
}
