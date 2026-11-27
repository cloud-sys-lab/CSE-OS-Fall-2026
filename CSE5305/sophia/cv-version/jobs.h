#ifndef JOBS_H
#define JOBS_H
//----------------------------------------------------------
// CV-Version
//----------------------------------------------------------

// Job Data Structure
//  - Represents a single unit of work that can be placed in the shared queue
//  - Worker threads dequeue jobs and execute the associated workload
typedef struct{
    int job_id; // Unique identifier used to distinguish jobs
    int work_size; // Amount of simulated computation required
} Job;

//----------------------------------------------------------

// Create and initialize a Job structure with a unique ID and work size
Job create_job(int job_id, int work_size);

// Executes the workload associated with a job
//  - Used to simulate CPU-bound processing without performing I/O operations
void execute_job(Job *job);

// Print job info (Job ID and Work Size)
void print_job(const Job *job);

#endif