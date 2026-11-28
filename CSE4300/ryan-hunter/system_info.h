#ifndef SYSTEM_INFO_H
#define SYSTEM_INFO_H

typedef struct {
    double cpu_percent;
    long mem_total_kb;
    long mem_available_kb;
    long mem_used_kb;
    double mem_percent;
    int num_cpus;
    int num_processes;
} SystemInfo;

int get_system_info(SystemInfo *info);
unsigned long long get_total_cpu_jiffies(void);
long get_total_memory_kb(void);

#endif
