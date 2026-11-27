#ifndef STATISTICS_H
#define STATISTICS_H

#include <pthread.h>
//----------------------------------------------------------
// Structure used to track scheduler statistics
typedef struct {
    // Counts how many jobs have finished execution
    int jobs_completed;

    // Stores the total execution time of all completed jobs (measured in milliseconds)
    long total_execution_time_ms;

    // Mutex Lock to protect statistics values from concurrent access by multiple worker threads
    pthread_mutex_t mutex;
} Statistics;

// Initializes all statistics values and create mutex
void init_statistics(Statistics *stats);

// Records information about a completed job
/*      - stats: Pointer to the shared statistics structure
        - execution_time_ms: Runtime of the completed job in milliseconds */
void record_job_completion(
    Statistics *stats,
    long execution_time_ms
);

// Prints summary of all collected scheduler statistics
void print_statistics(Statistics *stats);

#endif