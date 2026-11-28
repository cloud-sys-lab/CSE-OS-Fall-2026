#ifndef PROCESS_H
#define PROCESS_H

#include <sys/types.h>

#define PROC_NAME_MAX 256

typedef struct {
    pid_t pid;
    char name[PROC_NAME_MAX];
    char state;
    double cpu_percent;       /* 100% means one full CPU core. */
    double mem_percent;
    long mem_kb;              /* Linux reports these units as kB (1024 bytes). */
    unsigned long long start_time_ticks; /* Distinguishes reused PIDs. */
} Process;

const char *process_state_name(char state);
int get_process_list(Process *out, int max);
int get_process_info(pid_t pid, Process *p);
double get_process_cpu_usage(pid_t pid, int sample_ms);
double get_process_memory_usage(pid_t pid);
int terminate_process(pid_t pid, int force);
void cleanup_process_cache(void);

#endif
