#define _POSIX_C_SOURCE 200809L
#include "system_info.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int read_cpu_line(unsigned long long *total, unsigned long long *idle) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;
    unsigned long long user = 0, nice = 0, system = 0, idle_time = 0;
    unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0;
    int n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice, &system, &idle_time, &iowait, &irq, &softirq, &steal);
    fclose(f);
    if (n < 4) return -1;
    /* guest and guest_nice already contribute to user/nice; do not add twice. */
    *total = user + nice + system + idle_time + iowait + irq + softirq + steal;
    *idle = idle_time + iowait;
    return 0;
}

unsigned long long get_total_cpu_jiffies(void) {
    unsigned long long total, idle;
    return read_cpu_line(&total, &idle) == 0 ? total : 0;
}

static int read_memory(long *total, long *available) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    char key[64], line[256];
    long value, free_kb = 0, buffers = 0, cached = 0;
    *total = 0;
    *available = -1;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%63s %ld", key, &value) != 2 || value < 0) continue;
        if (strcmp(key, "MemTotal:") == 0) *total = value;
        else if (strcmp(key, "MemAvailable:") == 0) *available = value;
        else if (strcmp(key, "MemFree:") == 0) free_kb = value;
        else if (strcmp(key, "Buffers:") == 0) buffers = value;
        else if (strcmp(key, "Cached:") == 0) cached = value;
    }
    fclose(f);
    if (*total <= 0) return -1;
    if (*available < 0) *available = free_kb + buffers + cached;
    if (*available > *total) *available = *total;
    return 0;
}

long get_total_memory_kb(void) {
    long total, available;
    return read_memory(&total, &available) == 0 ? total : 0;
}

static int count_processes(void) {
    DIR *dir = opendir("/proc");
    if (!dir) return 0;
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *s = entry->d_name;
        if (!isdigit((unsigned char)*s)) continue;
        while (isdigit((unsigned char)*s)) ++s;
        if (*s == '\0') ++count;
    }
    closedir(dir);
    return count;
}

int get_system_info(SystemInfo *info) {
    static unsigned long long previous_total, previous_idle;
    static int have_previous;
    if (!info) return -1;
    memset(info, 0, sizeof *info);
    unsigned long long total, idle;
    if (read_cpu_line(&total, &idle) != 0 ||
        read_memory(&info->mem_total_kb, &info->mem_available_kb) != 0) return -1;
    if (have_previous && total > previous_total && idle >= previous_idle) {
        double dt = (double)(total - previous_total);
        double di = (double)(idle - previous_idle);
        info->cpu_percent = 100.0 * (dt - di) / dt;
        if (info->cpu_percent < 0.0) info->cpu_percent = 0.0;
        if (info->cpu_percent > 100.0) info->cpu_percent = 100.0;
    }
    previous_total = total;
    previous_idle = idle;
    have_previous = 1;
    info->mem_used_kb = info->mem_total_kb - info->mem_available_kb;
    info->mem_percent = 100.0 * (double)info->mem_used_kb / (double)info->mem_total_kb;
    info->num_cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (info->num_cpus < 1) info->num_cpus = 1;
    info->num_processes = count_processes();
    return 0;
}
