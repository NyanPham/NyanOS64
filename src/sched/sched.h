#ifndef SCHED_H
#define SCHED_H

#define TASK_READY 0x0
#define TASK_WAITING 0x1
#define TASK_DEAD 0x2
#define TASK_ZOMBIE 0x3
#define TASK_SLEEPING 0x4

#include "mem/vmm.h"
#include "fs/vfs.h"
#include "event/event.h"
#include "include/syscall_args.h"
#include <stdint.h>

#define MAX_OPEN_FILES 0x10
#define MAX_CWD_LEN 0x100

struct Window;

typedef struct Task
{
    uint64_t kern_stk_rsp;
    uint64_t kern_stk_top;
    int pid;
    short int state;
    struct Task *next;
    file_handle_t *fd_tbl[MAX_OPEN_FILES];
    uint64_t pml4;
    struct Task *parent;
    int ret_val;
    uint64_t heap_end;
    VmFreeRegion *vm_free_head;
    VmAllocatedList *vm_alloc_head;

    char name[64];
    char cwd[MAX_CWD_LEN];

    struct Window *win;

    uint32_t pending_signals;

    uint8_t fpu_regs[512 + 16];

    struct Task *wait_next;
    int64_t wake_tick;
    EventBuf *event_queue;
    int fg_pid;
} Task;

Task *sched_new_task(void);
void task_context_setup(Task *task, uint64_t entry, uint64_t rsp);
void task_context_reset(Task *task, uint64_t entry, uint64_t rsp);
void sched_destroy_task(Task *task);
void sched_unlink_task(Task *task);
void sched_init(void);
void schedule(void);
void task_idle(void);
void sched_block(void);
void sched_wake_pid(int pid);
void sched_exit(int code);
void sched_kill(int pid);
Task *get_curr_task(void);
Task *sched_find_task(int pid);
int64_t get_curr_task_pid(void);
void sched_send_signal(int pid, uint32_t sig_code);
void sched_reap_zombies(void);
int sched_get_procs(ProcInfo_t *out_buf, int max_count);

void sched_register_task(Task *task);
Task *task_factory_create(uint64_t entry, uint64_t rsp);
Task *task_factory_fork(Task *parent);
void sched_check_sleeping_tasks(void);

#endif