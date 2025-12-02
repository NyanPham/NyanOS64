#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>

typedef struct Task 
{
    uint64_t kern_stk_rsp;
    uint64_t kern_stk_top;
    int pid;
    struct Task* next;
} Task;

void sched_create_task(uint64_t entry);
void sched_init(void);
void schedule(void);
void sched_exit(void);

#endif