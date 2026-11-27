#include <stdio.h>
#include "jobs.h"
//----------------------------------------------------------
// CV-Version
//----------------------------------------------------------

// Create/initialize a new Job structure with a unique ID and amount of work to perform
Job create_job(int job_id, int work_size){
    Job job;

    job.job_id = job_id;
    job.work_size = work_size;

    return job;
}

//----------------------------------------------------------

// Simulates CPU-bound work using a computational loop
//  - No I/O operations are performed
//  - Larger work_size values require more iterations and therefore more CPU time
//  - Used to test thread scheduling and synchronization behavior
void execute_job(Job *job){
    // Volatile prevents the compiler from removing the loop as an optimization
    volatile unsigned long result = 0;

    for (int i = 0; i < job->work_size; i++){
        result += (i * i) % 97;
    }

    // Explicitly mark result as "used" so the compiler cannot remove the computation loop as dead code
    (void)result;
}

// Print job information ("job ID | work size")
void print_job(const Job *job){
    printf("Job ID: %d | Work Size: %d\n", job->job_id, job->work_size);
}
