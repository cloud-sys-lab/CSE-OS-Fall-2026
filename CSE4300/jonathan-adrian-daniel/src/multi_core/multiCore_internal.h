#ifndef MULTITHREAD_INTERNAL_H
#define MULTITHREAD_INTERNAL_H
//struct forwarding
struct scheduler;

typedef struct{
    struct scheduler* coreScheduler;
}core_attr;

#endif
