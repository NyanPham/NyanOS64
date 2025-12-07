#ifndef SCHED_H
#define SCHED_H

#define TASK_READY 0x0
#define TASK_WAITING 0x1
#define TASK_DEAD 0x2

#include <stdint.h>

typedef struct Task 
{
    uint64_t kern_stk_rsp;
    uint64_t kern_stk_top;
    int pid;
    short int state;
    struct Task* next;
} Task;

void sched_create_task(uint64_t entry);
void sched_init(void);
void schedule(void);
void task_idle(void);
void sched_block();
void sched_wake_pid(int pid);
void sched_exit(void);

#endif