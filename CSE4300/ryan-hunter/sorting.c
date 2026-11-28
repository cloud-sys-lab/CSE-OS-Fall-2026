#include "sorting.h"
#include <stdlib.h>
#include <strings.h>

static int tiebreaker(const Process *a, const Process *b) {
    return (a->pid > b->pid) - (a->pid < b->pid);
}
static int cmp_cpu(const void *pa, const void *pb) {
    const Process *a = pa, *b = pb;
    if (a->cpu_percent != b->cpu_percent)
        return (a->cpu_percent < b->cpu_percent) - (a->cpu_percent > b->cpu_percent);
    return tiebreaker(a, b);
}
static int cmp_mem(const void *pa, const void *pb) {
    const Process *a = pa, *b = pb;
    if (a->mem_kb != b->mem_kb)
        return (a->mem_kb < b->mem_kb) - (a->mem_kb > b->mem_kb);
    return tiebreaker(a, b);
}
static int cmp_pid(const void *pa, const void *pb) { return tiebreaker(pa, pb); }
static int cmp_name(const void *pa, const void *pb) {
    const Process *a = pa, *b = pb;
    int c = strcasecmp(a->name, b->name);
    return c ? c : tiebreaker(a, b);
}
void sort_processes(Process *list, int count, SortType type) {
    if (!list || count < 2) return;
    int (*cmp)(const void *, const void *);
    switch (type) {
        case SORT_CPU: cmp = cmp_cpu; break;
        case SORT_MEM: cmp = cmp_mem; break;
        case SORT_PID: cmp = cmp_pid; break;
        case SORT_NAME: cmp = cmp_name; break;
        default: return;
    }
    qsort(list, (size_t)count, sizeof *list, cmp);
}
const char *sort_type_name(SortType type) {
    switch (type) {
        case SORT_CPU: return "CPU";
        case SORT_MEM: return "Memory";
        case SORT_PID: return "PID";
        case SORT_NAME: return "Name";
        default: return "Unknown";
    }
}
int parse_sort_type(const char *name, SortType *out) {
    if (!name || !out) return -1;
    if (!strcasecmp(name, "cpu")) *out = SORT_CPU;
    else if (!strcasecmp(name, "mem") || !strcasecmp(name, "memory")) *out = SORT_MEM;
    else if (!strcasecmp(name, "pid")) *out = SORT_PID;
    else if (!strcasecmp(name, "name")) *out = SORT_NAME;
    else return -1;
    return 0;
}
