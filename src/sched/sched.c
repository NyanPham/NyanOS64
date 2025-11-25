#include "sched.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "mem/kmalloc.h"
#include "arch/gdt.h"
#include <stddef.h>

#define NEW_TASK_SS (GDT_OFFSET_USER_DATA | 0x3)
#define NEW_TASK_CS (GDT_OFFSET_USER_CODE | 0x3)
#define NEW_TASK_RFLAGS 0x202
#define KERN_TASK_PID 0x0

// Linked list for Tasks
static Task* g_head_task = NULL;
static Task* g_curr_task = NULL;
static int g_next_pid = KERN_TASK_PID + 1;      // value "0" is our OS kernel

extern uint64_t hhdm_offset;
extern uint64_t* kern_pml4;
extern void tss_set_stack(uint64_t stk_ptr);
extern uint64_t kern_stk_ptr;

extern void switch_to_task(uint64_t* prev_rsp_ptr, uint64_t next_rsp);
extern void task_start_stub(void);

/*
 * Allocs mem for a Task
 * Assign a part of Kernel Stack to handle when interrupted
 * Allocs user stack for the program to run
 * Make fake task scene
 */
void sched_create_task(uint64_t entry)
{
    // construct a new task
    Task* new_task = (Task*)kmalloc(sizeof(Task));
    new_task->pid = g_next_pid++;
    new_task->next = NULL;

    // alloc a page for within the Kernel Stack
    // Note: kern_stk is virt hhdm addr.
    void* kern_stk = pmm_alloc_frame();
    uint64_t* sp = (uint64_t*)((uint8_t*)kern_stk + PAGE_SIZE);
    new_task->kern_stk_top = (uint64_t)sp;

    // alloc a page for User Stack, map it with a virt addr
    uint64_t virt_usr_stk = 0x500000 + (new_task->pid * 0x10000);
    uint64_t phys_usr_stk = (uint64_t)pmm_alloc_frame() - hhdm_offset;

    vmm_map_page(
        kern_pml4,
        virt_usr_stk,
        phys_usr_stk,
        VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER
    );

    // now make a fake scene from scratch for the new task
    *(--sp) = NEW_TASK_SS;              // SS
    *(--sp) = virt_usr_stk + PAGE_SIZE; // RSP
    *(--sp) = NEW_TASK_RFLAGS;          // RFLAGS
    *(--sp) = NEW_TASK_CS;              // CS
    *(--sp) = entry;                    // RIP

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
    new_task->kern_stk_rsp = (uint64_t)sp;

    // add to the linked list of Tasks
    if (g_head_task == NULL)
    {
        g_head_task = new_task;
        new_task->next = new_task; // (loop)
        g_curr_task = new_task;
    } 
    else
    {
        new_task->next = g_head_task->next;
        g_head_task->next = new_task;
    }
}

void sched_init(void)
{
    /*
    When our OS starts running, it's the very first program, but annonymous (no pid)
    so we need to keep track our OS as Task 0 (Kernel Task).
    */

    Task* kern_task = (Task*)kmalloc(sizeof(Task));
    kern_task->pid = 0;
    kern_task->kern_stk_rsp = 0;
    kern_task->kern_stk_top = 0;
    kern_task->next = kern_task;
    g_head_task = kern_task;
    g_curr_task = kern_task; 
}

void schedule(void)
{
    if (g_curr_task == NULL)
    {
        return;
    }
    
    Task* next_task = g_curr_task->next;
    if (next_task == g_curr_task) 
    {
        kprint("Only 1 task, no switch!\n");
        return;
    }

    Task* prev_task = g_curr_task;
    g_curr_task = next_task;
    tss_set_stack(next_task->kern_stk_top);
    kern_stk_ptr = next_task->kern_stk_top;
    /*
    Context Switching
    We do save current RSP value into &prev_task->kern_stk_rsp
    and pass new RSP value from next_task->kern_stk_rsp
    */
    switch_to_task(&prev_task->kern_stk_rsp, next_task->kern_stk_rsp);
}