#ifndef WORKLOADS_H
#define WORKLOADS_H

#define MAX_WORKLOAD_JOBS 20

typedef enum
{
    WORKLOAD_SMALL,
    WORKLOAD_BALANCED,
    WORKLOAD_HEAVY,
    WORKLOAD_MIXED
} WorkloadType;

typedef struct
{
    int id;
    int size;
} WorkloadJob;

/*
 * Fills jobs[] with workload data.
 * Returns number of jobs created.
 */
int create_workload(WorkloadJob jobs[],
                    WorkloadType type);

#endif