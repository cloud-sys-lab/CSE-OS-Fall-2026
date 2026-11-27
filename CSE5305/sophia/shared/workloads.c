#include "workloads.h"

int create_workload(WorkloadJob jobs[], WorkloadType type){
    switch(type){
        case WORKLOAD_SMALL:
            jobs[0] = (WorkloadJob){1, 1000000};
            jobs[1] = (WorkloadJob){2, 1000000};
            jobs[2] = (WorkloadJob){3, 1000000};

            return 3;

        case WORKLOAD_BALANCED:
            jobs[0] = (WorkloadJob){1, 1000000};
            jobs[1] = (WorkloadJob){2, 5000000};
            jobs[2] = (WorkloadJob){3,10000000};
            jobs[3] = (WorkloadJob){4, 5000000};
            jobs[4] = (WorkloadJob){5, 1000000};

            return 5;

        case WORKLOAD_HEAVY:
            jobs[0] = (WorkloadJob){1,20000000};
            jobs[1] = (WorkloadJob){2,30000000};
            jobs[2] = (WorkloadJob){3,40000000};
            jobs[3] = (WorkloadJob){4,50000000};

            return 4;

        case WORKLOAD_MIXED:
            jobs[0] = (WorkloadJob){1,50000};
            jobs[1] = (WorkloadJob){2,3000000};
            jobs[2] = (WorkloadJob){3,100000};
            jobs[3] = (WorkloadJob){4,4000000};
            jobs[4] = (WorkloadJob){5,50000};

            return 5;

        default:
            return 0;
    }
}