#ifndef REFRESH_H
#define REFRESH_H
#include "control.h"
#define MAX_PROCESSES 8192
typedef struct {
    int batch;
    int iterations;
    int limit;
    int interval_ms;
    SortType sort;
    char query[QUERY_MAX];
} RunOptions;
void default_run_options(RunOptions *options);
int run_interface(void);
int run_with_options(const RunOptions *options);
#endif
