#ifndef SCHED_H
#define SCHED_H

#define TASK_READY 0x0
#define TASK_WAITING 0x1
#define TASK_DEAD 0x2
#define TASK_ZOMBIE 0x3

#include "fs/vfs.h"
#include <stdint.h>

#define MAX_OPEN_FILES 0x10 // each task has at most 16 files open
#define MAX_CWD_LEN 0x100

// Forward declartion, not include window.h to avoid circular dependency
struct Window;
struct Terminal;

typedef struct Task
{
    uint64_t kern_stk_rsp;
    uint64_t kern_stk_top;
    int pid;
    short int state;
    struct Task *next;
    file_handle_t *fd_tbl[MAX_OPEN_FILES];
    uint64_t pml4; // phys_addr of pml4
    struct Task *parent;
    int ret_val; // exit code
    uint64_t heap_end;
    char cwd[MAX_CWD_LEN];

    // GUI & CLI
    struct Window *win;    // For GUI app
    struct Terminal *term; // For CLI app (.e.g Shell)

    // Signal
    uint32_t pending_signals;
} Task;

Task *sched_new_task(void);
void task_context_setup(Task *task, uint64_t entry, uint64_t rsp);
void task_context_reset(Task *task, uint64_t entry, uint64_t rsp);
void sched_destroy_task(Task *task);
void sched_unlink_task(Task *task);
void sched_init(void);
void schedule(void);
void task_idle(void);
void sched_block();
void sched_wake_pid(int pid);
void sched_exit(int code);
void sched_kill(int pid);
Task *get_curr_task(void);
Task *sched_find_task(int pid);
int64_t get_curr_task_pid();
void sched_send_signal(int pid, uint32_t sig_code);

void sched_register_task(Task *task);
Task *task_factory_create(uint64_t entry, uint64_t rsp);
Task *task_factory_fork(Task* parent);

static void inline sched_clean_gui(Task *tsk);

#endif