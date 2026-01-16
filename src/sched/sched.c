#include "sched.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "mem/kmalloc.h"
#include "arch/gdt.h"
#include "drivers/serial.h"
#include "cpu.h"
#include "kern_defs.h"
#include "fs/dev.h"
#include "gui/window.h"
#include "gui/terminal.h"
#include "include/signal.h"

#include <stddef.h>

#define NEW_TASK_SS (GDT_OFFSET_USER_DATA | 0x3)
#define NEW_TASK_CS (GDT_OFFSET_USER_CODE | 0x3)
#define NEW_TASK_RFLAGS 0x202
#define KERN_TASK_PID 0x0

// Linked list for Tasks
static Task *g_head_tsk = NULL;
static Task *g_curr_tsk = NULL;
static int g_next_pid = KERN_TASK_PID + 1; // value "0" is our OS kernel

extern uint64_t hhdm_offset;
extern uint64_t *kern_pml4;
extern void tss_set_stack(uint64_t stk_ptr);
extern uint64_t kern_stk_ptr;

extern void switch_to_task(uint64_t *prev_rsp_ptr, uint64_t next_rsp);
extern void task_start_stub(void);

/*
 * Allocs mem for a Task
 * Creates a new Task with its own PID, cr3.
 * The state must be TASK_WAITING, because the
 * task still needs to has `entry` setup before
 * being TASK_READY
 */
Task *sched_new_task(void)
{
    Task *new_tsk = (Task *)kmalloc(sizeof(Task));
    new_tsk->pid = g_next_pid++;
    new_tsk->next = NULL;
    new_tsk->kern_stk_top = 0;
    new_tsk->state = TASK_WAITING;
    new_tsk->pml4 = vmm_new_pml4();
    new_tsk->parent = g_curr_tsk;
    new_tsk->heap_end = USER_HEAP_START;
    new_tsk->win = NULL;
    new_tsk->term = NULL;
    new_tsk->pending_signals = 0;

    if (g_curr_tsk != NULL) // has parent -> copy dir from him
    {
        strcpy(new_tsk->cwd, g_curr_tsk->cwd);
        new_tsk->win = g_curr_tsk->win;
        new_tsk->term = g_curr_tsk->term;
    }
    else // else, it's the first task, root is "/"
    {
        strcpy(new_tsk->cwd, "/");
    }

    for (uint8_t i = 0; i < MAX_OPEN_FILES; i++)
    {
        new_tsk->fd_tbl[i] = NULL;
    }

    dev_attach_stdio(new_tsk->fd_tbl);

    return new_tsk;
}

/*
 * Assigns a part of Kernel Stack to handle when interrupted
 * Allocs user stack for the program to run
 * Makes fake task scene
 */
void task_context_setup(Task *tsk, uint64_t entry, uint64_t rsp)
{
    // uint64_t curr_pml4 = read_cr3();
    // write_cr3(tsk->pml4);

    // alloc a page for within the Kernel Stack
    // Note: kern_stk is virt hhdm addr.
    void *kern_stk = pmm_alloc_frame();
    uint64_t *sp = (uint64_t *)((uint8_t *)kern_stk + PAGE_SIZE);
    tsk->kern_stk_top = (uint64_t)sp;

    // now make a fake scene from scratch for the new task
    *(--sp) = NEW_TASK_SS;     // SS
    *(--sp) = rsp;             // RSP
    *(--sp) = NEW_TASK_RFLAGS; // RFLAGS
    *(--sp) = NEW_TASK_CS;     // CS
    *(--sp) = entry;           // RIP

    // we push rax -> r15, they're all zeros, so order doesn't matter anyway.
    for (uint8_t i = 0; i < 15; i++)
    {
        *(--sp) = 0;
    }

    // return address for a new switch_to_task's RET
    *(--sp) = (uint64_t)task_start_stub;

    for (uint8_t i = 0; i < 6; i++)
    {
        *(--sp) = 0;
    }

    // now the top of the Kernel Stack is to store the whole state of the task
    tsk->kern_stk_rsp = (uint64_t)sp;
    // write_cr3(curr_pml4);
}

/*
 * Frees the kernel Stack to PMM
 * Returns the PML4 page (recursively) to the PMM
 * Frees the struct Task
 */
void sched_destroy_task(Task *tsk)
{
    if (tsk->kern_stk_top != 0)
    {
        pmm_free_frame((void *)(tsk->kern_stk_top - PAGE_SIZE));
    }

    vmm_ret_pml4(tsk->pml4);

    sched_clean_gui(tsk);
    kfree(tsk);
}

void sched_unlink_task(Task *tsk)
{
    // find the task's predecessor
    Task *prev = tsk;
    while (prev->next != tsk)
    {
        prev = prev->next;
    }

    // unlink it
    Task *next_tsk = tsk->next;
    prev->next = next_tsk;

    if (tsk == g_head_tsk)
    {
        g_head_tsk = next_tsk;
        if (tsk == next_tsk)
        {
            g_head_tsk = NULL;
        }
    }
}

void sched_init(void)
{
    /*
    When our OS starts running, it's the very first program, but annonymous (no pid)
    so we need to keep track our OS as Task 0 (Kernel Task).
    */

    Task *kern_task = (Task *)kmalloc(sizeof(Task));
    kern_task->pid = 0;
    kern_task->state = TASK_READY;
    kern_task->pml4 = read_cr3();

    for (uint8_t i = 0; i < MAX_OPEN_FILES; i++)
    {
        kern_task->fd_tbl[i] = NULL;
    }
    kern_task->kern_stk_rsp = 0;
    kern_task->kern_stk_top = 0;

    // void* stk_phys = pmm_alloc_frame();
    // kern_task->kern_stk_top = (uint64_t)stk_phys + PAGE_SIZE;

    // uint64_t* sp = (uint64_t*)kern_task->kern_stk_top;
    // *(--sp) = (uint64_t)task_idle;
    // *(--sp) = 0; // RBX
    // *(--sp) = 0; // RBP
    // *(--sp) = 0; // R12
    // *(--sp) = 0; // R13
    // *(--sp) = 0; // R14
    // *(--sp) = 0; // R15
    // kern_task->kern_stk_rsp = (uint64_t)sp;
    kern_task->next = kern_task;
    g_head_tsk = kern_task;
    g_curr_tsk = kern_task;
}

void task_idle(void)
{
    asm volatile("sti");
    for (;;)
    {
        asm("hlt");
    }
}

void schedule(void)
{
    Task *next_tsk;

    if (g_curr_tsk == NULL)
    {
        next_tsk = g_head_tsk;
    }
    else
    {
        next_tsk = g_curr_tsk->next;

        while (next_tsk->state != TASK_READY)
        {
            next_tsk = next_tsk->next;
        }

        if (next_tsk == g_curr_tsk)
        {
            // kprint("Only 1 task, no switch!\n");
            return;
        }
    }

    Task *prev_task = g_curr_tsk;
    g_curr_tsk = next_tsk;

    tss_set_stack(next_tsk->kern_stk_top);
    kern_stk_ptr = next_tsk->kern_stk_top;

    // Process Isolation
    // store the pml4 of the task to the CR3
    write_cr3(next_tsk->pml4);

    /*
    Context Switching
    We do save current RSP value into &prev_task->kern_stk_rsp
    and pass new RSP value from next_tsk->kern_stk_rsp
    */
    if (prev_task == NULL)
    {
        switch_to_task(NULL, next_tsk->kern_stk_rsp);
    }
    else
    {
        switch_to_task(&prev_task->kern_stk_rsp, next_tsk->kern_stk_rsp);
    }

    // CPU is now running g_curr_tsk, which is the next_tsk right above this comment.
    // The g_curr_tsk just woke up after being putting to sleep for some time.
    // This is the best place to check for Pending Signals

    if (g_curr_tsk->pending_signals & (1 << (SIGINT - 1)))
    {
        kprint("Signal SIGINT detected! Killing process...\n");
        g_curr_tsk->pending_signals &= ~(1 << (SIGINT - 1));
        sched_exit(0);
    }
}

void sched_block()
{
    g_curr_tsk->state = TASK_WAITING;
    schedule();
}

void sched_wake_pid(int pid)
{
    Task *t = sched_find_task(pid);
    if (t == NULL)
    {
        return;
    }

    t->state = TASK_READY;
}

void sched_exit(int code)
{
    if (g_curr_tsk->pid == 0)
    {
        kprint("Kernel cannot exit!\n");
        return;
    }

    // unlink it
    Task *task_to_exit = g_curr_tsk;
    Task *next_tsk = task_to_exit->next;

    while (next_tsk->state != TASK_READY)
    {
        next_tsk = next_tsk->next;
        if (next_tsk == task_to_exit)
        {
            kprint("PANIC: NO valid task to switch to!\n");
            for (;;)
                ;
        }
    }

    if (g_head_tsk == task_to_exit)
    {
        g_head_tsk = next_tsk;
    }

    g_curr_tsk = next_tsk;

    tss_set_stack(next_tsk->kern_stk_top);
    kern_stk_ptr = next_tsk->kern_stk_top;

    write_cr3(next_tsk->pml4);

    // sched_destroy_task(task_to_exit);
    task_to_exit->state = TASK_ZOMBIE;
    task_to_exit->ret_val = code;
    sched_wake_pid(task_to_exit->parent->pid);
    sched_clean_gui(task_to_exit);

    kprint("Task exited. Switching to next...");
    switch_to_task(NULL, next_tsk->kern_stk_rsp);
}

void sched_kill(int pid)
{
    // Suicide
    if (pid == g_curr_tsk->pid)
    {
        sched_exit(-1);
        return;
    }

    // Assassinate
    Task *tgt_tsk = sched_find_task(pid);
    if (tgt_tsk == NULL)
    {
        kprint("SCHED: Kill failed, PID not found\n");
        return;
    }

    // close the ui immediately to make it look fast
    sched_clean_gui(tgt_tsk);
    tgt_tsk->state = TASK_ZOMBIE;
    tgt_tsk->ret_val = -1;

    if (tgt_tsk->parent != NULL)
    {
        sched_wake_pid(tgt_tsk->parent->pid);
    }
}

Task *get_curr_task()
{
    return g_curr_tsk;
}

Task *sched_find_task(int pid)
{
    if (g_head_tsk == NULL)
    {
        return NULL;
    }

    Task *t = g_head_tsk;
    do
    {
        if (t->pid == pid)
        {
            return t;
        }
        t = t->next;
    } while (t != g_head_tsk);

    return NULL;
}

int64_t get_curr_task_pid()
{
    Task *t = get_curr_task();
    if (t == NULL)
    {
        return -1;
    }

    return t->pid;
}

void sched_send_signal(int pid, uint32_t sig_code)
{
    Task *tsk = sched_find_task(pid);
    if (tsk == NULL)
    {
        kprint("SCHED_SEND_SIGNAL failed: No task found for the id\n");
        return;
    }

    tsk->pending_signals |= 1 << (sig_code - 1);
}

/**
 * @brief Registers a task to the linked list of tasks
 */
void sched_register_task(Task *tsk)
{
    if (g_head_tsk == NULL)
    {
        g_head_tsk = tsk;
        tsk->next = tsk; // (loop)
        g_curr_tsk = tsk;
    }
    else
    {
        tsk->next = g_head_tsk->next;
        g_head_tsk->next = tsk;
    }

    tsk->state = TASK_READY;
}

/**
 * @brief Creates a task
 * Allocs a new task, and fills the stack
 * and fake scene to kickstart using iretq.
 */
Task *task_factory_create(uint64_t entry, uint64_t rsp)
{
    Task *tsk = sched_new_task();
    if (tsk == NULL)
    {
        return NULL;
    }

    task_context_setup(tsk, entry, rsp);
    return tsk;
}

/**
 * @brief Forks a child task from a parent task
 * Creates a new task a child.
 * Deep-copies from the parent task.
 * Sets up correct trapframe,
 * especially rax of parent is the child's pid
 * but rax of child is 0.
 */
Task *task_factory_fork(Task *parent_tsk)
{
    Task *child_tsk = sched_new_task();
    if (child_tsk == NULL)
    {
        return NULL;
    }

    // copy the memory space
    uint64_t new_pml4 = vmm_copy_hierarchy((uint64_t *)(parent_tsk->pml4 + hhdm_offset), 4);
    child_tsk->pml4 = new_pml4;

    // alloc kernel stack for the child
    void *kern_stk = pmm_alloc_frame();
    if (kern_stk == NULL)
    {
        kfree(child_tsk);
        return NULL;
    }
    child_tsk->kern_stk_top = (uint64_t)kern_stk + PAGE_SIZE;

    // copy environment
    memcpy(child_tsk->fd_tbl, parent_tsk->fd_tbl, MAX_OPEN_FILES * sizeof(file_handle_t *));
    memcpy(child_tsk->cwd, parent_tsk->cwd, MAX_CWD_LEN);
    child_tsk->heap_end = parent_tsk->heap_end;
    child_tsk->parent = parent_tsk;
    child_tsk->state = TASK_READY;
    child_tsk->win = parent_tsk->win;
    child_tsk->term = parent_tsk->term;

    // convert the parent's syscall stack to the child's iretq stack
    uint64_t *parent_sp = (uint64_t *)parent_tsk->kern_stk_top - 13; // right rsp is at R15, totally 8 pushes to the sp
    uint64_t *child_sp = (uint64_t *)child_tsk->kern_stk_top;

    // SS User data
    *(--child_sp) = NEW_TASK_SS;

    // User RSP in syscall_entry
    uint64_t parent_user_rsp = *(parent_sp + 12);
    *(--child_sp) = parent_user_rsp;

    // R11 (RFLAGS) in syscall_entry
    *(--child_sp) = (uint64_t)*(parent_sp + 10) | 0x200; // turn the interrupt on

    // CS usercode
    *(--child_sp) = NEW_TASK_CS;

    // RCX (RIP) in syscall_entry
    *(--child_sp) = (uint64_t)*(parent_sp + 11);

    // copy GPRs for task_start_stub POP
    *(--child_sp) = 0;                           // pid, rax = 0;
    *(--child_sp) = (uint64_t)*(parent_sp + 7);  // rbx
    *(--child_sp) = (uint64_t)*(parent_sp + 11); // rcx
    *(--child_sp) = 0;                           // rdx
    *(--child_sp) = 0;                           // rsi
    *(--child_sp) = 0;                           // rdi
    *(--child_sp) = (uint64_t)*(parent_sp + 8);  // rbp
    *(--child_sp) = (uint64_t)*(parent_sp + 6);  // r8
    *(--child_sp) = (uint64_t)*(parent_sp + 5);  // r9
    *(--child_sp) = (uint64_t)*(parent_sp + 4);  // r10
    *(--child_sp) = (uint64_t)*(parent_sp + 10); // r11
    *(--child_sp) = (uint64_t)*(parent_sp + 3);  // r12
    *(--child_sp) = (uint64_t)*(parent_sp + 2);  // r13
    *(--child_sp) = (uint64_t)*(parent_sp + 1);  // r14
    *(--child_sp) = (uint64_t)*(parent_sp + 0);  // r15

    *(--child_sp) = (uint64_t)task_start_stub;

    // init callee-saved regs for switch_to_task pop
    for (int i = 0; i < 6; i++)
    {
        *(--child_sp) = 0;
    }

    child_tsk->kern_stk_rsp = (uint64_t)child_sp;

    return child_tsk;
}

static void inline sched_clean_gui(Task *tsk)
{
    asm volatile("cli");
    // Handle GUI vs CLI
    if (tsk->term != NULL)
    {
        if (tsk->term->child_pid == tsk->pid)
        {
            if (tsk->parent != NULL)
            {
                tsk->term->child_pid = tsk->parent->pid;
            }
            else
            {
                tsk->term->child_pid = -1;
            }
        }
        if (tsk->term->win != NULL && tsk->term->win->owner_pid == tsk->pid)
        {
            term_destroy(tsk->term);
        }
        tsk->term = NULL;
        tsk->win = NULL;
    }

    if (tsk->win != NULL)
    {
        if (tsk->win->owner_pid == tsk->pid)
        {
            win_close(tsk->win);
        }
        tsk->win = NULL;
    }
}