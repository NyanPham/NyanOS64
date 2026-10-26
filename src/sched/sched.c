#include "sched.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "mem/kmalloc.h"
#include "arch/gdt.h"
#include "drivers/timer.h"
#include "drivers/serial.h"
#include "cpu.h"
#include "kern_defs.h"
#include "fs/dev.h"
#include "gui/window.h"
#include "include/signal.h"
#include "utils/asm_instrs.h"
#include "../string.h"
#include "utils/spinlock.h"

#include <stddef.h>

#define NEW_TASK_SS (GDT_OFFSET_USER_DATA | 0x3)
#define NEW_TASK_CS (GDT_OFFSET_USER_CODE | 0x3)
#define NEW_TASK_RFLAGS 0x202
#define KERN_TSK_PID 0x0

static spinlock_t sched_lock = SPINLOCK_INIT;

static Task *g_head_tsk = NULL;
static Task *g_curr_tsk = NULL;
static int g_next_pid = KERN_TSK_PID + 1;

extern uint64_t *kern_pml4;
extern void tss_set_stack(uint64_t stk_ptr);
extern uint64_t kern_stk_ptr;

extern void switch_to_task(uint64_t *prev_rsp_ptr, uint64_t next_rsp, uint8_t *prev_fpu, uint8_t *next_fpu);
extern void task_start_stub(void);

inline static void sched_clean_gui(Task *tsk);
inline static void sched_clean_fds(Task *tsk);

void sched_acquire_lock(uint64_t *rflags)
{
    SPIN_LOCK_IRQSAVE(&sched_lock, *rflags);
}

void sched_release_lock(uint64_t *rflags)
{
    SPIN_UNLOCK_IRQRESTORE(&sched_lock, *rflags);
}

Task *sched_find_task_locked(int pid)
{
    if (g_head_tsk == NULL)
        return NULL;
    Task *t = g_head_tsk;
    do
    {
        if (t->pid == pid)
            return t;
        t = t->next;
    } while (t != g_head_tsk);
    return NULL;
}

static uint8_t *get_aligned_fpu_region(Task *tsk)
{
    uint64_t addr = (uint64_t)tsk->fpu_regs;
    addr = (addr + 15) & ~((uint64_t)0xF);
    return (uint8_t *)addr;
}

static void sched_reparent_children_locked(Task *exiting_tsk)
{
    if (g_head_tsk == NULL)
    {
        return;
    }
    bool is_win_owner = (exiting_tsk->win != NULL && exiting_tsk->win->owner_pid == exiting_tsk->pid);
    Task *t = g_head_tsk;
    do
    {
        if (t->parent == exiting_tsk)
        {
            t->parent = g_head_tsk;
            if (is_win_owner && t->pid != 0 && t->state != TASK_ZOMBIE && t->state != TASK_DEAD)
            {
                t->pending_signals |= (1 << (SIGINT - 1));
                t->state = TASK_READY;
            }
        }
        t = t->next;
    } while (t != g_head_tsk);
}

Task *sched_new_task(void)
{
    Task *new_tsk = (Task *)kmalloc(sizeof(Task));
    if (new_tsk == NULL)
    {
        return NULL;
    }
    memset(new_tsk, 0, sizeof(Task));

    uint64_t flags;
    SPIN_LOCK_IRQSAVE(&sched_lock, flags);
    new_tsk->pid = g_next_pid++;
    SPIN_UNLOCK_IRQRESTORE(&sched_lock, flags);

    new_tsk->next = NULL;
    new_tsk->kern_stk_top = 0;
    new_tsk->state = TASK_WAITING;
    new_tsk->pml4 = vmm_new_pml4();
    if (new_tsk->pml4 == 0)
    {
        kfree(new_tsk);
        return NULL;
    }
    new_tsk->parent = g_curr_tsk;
    new_tsk->heap_end = USER_HEAP_START;
    new_tsk->win = NULL;
    new_tsk->pending_signals = 0;
    new_tsk->wait_next = NULL;
    new_tsk->wake_tick = -1;
    new_tsk->fg_pid = -1;

    uint8_t *fpu_ptr = get_aligned_fpu_region(new_tsk);
    memset(fpu_ptr, 0, 512);
    *((uint16_t *)(fpu_ptr)) = 0x037F;
    *((uint32_t *)(fpu_ptr + 0x18)) = 0x1F80;

    VmFreeRegion *vm_free_head = (VmFreeRegion *)kmalloc(sizeof(VmFreeRegion));
    if (vm_free_head == NULL)
    {
        vmm_ret_pml4(new_tsk->pml4);
        kfree(new_tsk);
        return NULL;
    }

    new_tsk->vm_free_head = vm_free_head;
    new_tsk->vm_free_head->addr = USER_MMAP_START;
    new_tsk->vm_free_head->size = USER_MMAP_SIZE;
    new_tsk->vm_free_head->next = NULL;
    new_tsk->vm_alloc_head = NULL;

    EventBuf *event_queue = (EventBuf *)vmm_alloc_global(sizeof(EventBuf));
    if (event_queue == NULL)
    {
        kfree(vm_free_head);
        vmm_ret_pml4(new_tsk->pml4);
        kfree(new_tsk);
        return NULL;
    }
    new_tsk->event_queue = event_queue;
    event_queue_init(new_tsk->event_queue);

    if (g_curr_tsk != NULL && g_curr_tsk->cwd[0] != '\0')
    {
        strcpy(new_tsk->cwd, g_curr_tsk->cwd);
    }
    else
    {
        strcpy(new_tsk->cwd, "/");
    }

    if (g_curr_tsk != NULL && g_curr_tsk->name[0] != '\0')
    {
        strncpy(new_tsk->name, g_curr_tsk->name, 63);
        new_tsk->name[63] = '\0';
    }
    else
    {
        strcpy(new_tsk->name, "user_task");
    }

    for (uint8_t i = 0; i < MAX_OPEN_FILES; i++)
    {
        new_tsk->fd_tbl[i] = NULL;
    }
    dev_attach_stdio(new_tsk->fd_tbl);

    return new_tsk;
}

void task_context_setup(Task *tsk, uint64_t entry, uint64_t rsp)
{
    uint64_t kern_stk = pmm_alloc_frame();
    if (kern_stk == 0)
        return;

    uint64_t *sp = (uint64_t *)((uint8_t *)vmm_phys_to_hhdm(kern_stk) + PAGE_SIZE);
    tsk->kern_stk_top = (uint64_t)sp;

    *(--sp) = NEW_TASK_SS;     // SS
    *(--sp) = rsp;             // RSP
    *(--sp) = NEW_TASK_RFLAGS; // RFLAGS
    *(--sp) = NEW_TASK_CS;     // CS
    *(--sp) = entry;           // RIP

    for (uint8_t i = 0; i < 15; i++)
    {
        *(--sp) = 0;
    }
    *(--sp) = (uint64_t)task_start_stub;
    for (uint8_t i = 0; i < 6; i++)
    {
        *(--sp) = 0;
    }

    tsk->kern_stk_rsp = (uint64_t)sp;
}

void task_context_reset(Task *tsk, uint64_t entry, uint64_t rsp)
{
    uint64_t *sp = (uint64_t *)tsk->kern_stk_top;
    *(--sp) = NEW_TASK_SS;
    *(--sp) = rsp;
    *(--sp) = NEW_TASK_RFLAGS;
    *(--sp) = NEW_TASK_CS;
    *(--sp) = entry;

    for (int i = 0; i < 15; i++)
    {
        *(--sp) = 0;
    }
    *(--sp) = (uint64_t)task_start_stub;
    for (int i = 0; i < 6; i++)
    {
        *(--sp) = 0;
    }

    tsk->kern_stk_rsp = (uint64_t)sp;
}

void sched_destroy_task(Task *tsk)
{
    if (tsk == NULL || tsk->pid == 0)
        return;

    sched_clean_gui(tsk);
    sched_clean_fds(tsk);

    if (tsk->kern_stk_top != 0)
    {
        pmm_free_frame(vmm_hhdm_to_phys((void *)(tsk->kern_stk_top - PAGE_SIZE)));
        tsk->kern_stk_top = 0;
    }
    if (tsk->event_queue != NULL)
    {
        vmm_free(tsk->event_queue);
        tsk->event_queue = NULL;
    }
    vmm_cleanup_task(tsk);
    if (tsk->pml4 != 0)
    {
        vmm_free_table(vmm_phys_to_hhdm(tsk->pml4), 4);
        tsk->pml4 = 0;
    }
    kfree(tsk);
}

void sched_unlink_task(Task *tsk)
{
    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(&sched_lock, rflags);

    if (g_head_tsk == NULL)
    {
        SPIN_UNLOCK_IRQRESTORE(&sched_lock, rflags);
        return;
    }

    Task *prev = g_head_tsk;
    while (prev->next != tsk && prev->next != g_head_tsk)
    {
        prev = prev->next;
    }

    if (prev->next == tsk)
    {
        prev->next = tsk->next;
        if (tsk == g_head_tsk)
        {
            g_head_tsk = (tsk->next == tsk) ? NULL : tsk->next;
        }
    }

    SPIN_UNLOCK_IRQRESTORE(&sched_lock, rflags);
}

void sched_init(void)
{
    Task *kern_tsk = (Task *)kmalloc(sizeof(Task));
    memset(kern_tsk, 0, sizeof(Task));
    kern_tsk->pid = 0;
    kern_tsk->state = TASK_READY;
    kern_tsk->pml4 = read_cr3();
    kern_tsk->wake_tick = -1;
    kern_tsk->fg_pid = -1;
    strcpy(kern_tsk->cwd, "/");
    strcpy(kern_tsk->name, "kernel");

    EventBuf *event_queue = (EventBuf *)vmm_alloc_global(sizeof(EventBuf));
    if (event_queue == NULL)
    {
        kprint("PANIC: Kernel Task Stack for EVENT_QUEUE: OOM\n");
        hcf();
    }
    kern_tsk->event_queue = event_queue;
    event_queue_init(kern_tsk->event_queue);

    for (uint8_t i = 0; i < MAX_OPEN_FILES; i++)
    {
        kern_tsk->fd_tbl[i] = NULL;
    }

    uint64_t stk_phys = pmm_alloc_frame();
    if (stk_phys == 0)
    {
        kprint("PANIC: Kernel Task Stack OOM\n");
        hcf();
    }
    kern_tsk->kern_stk_top = (uint64_t)vmm_phys_to_hhdm(stk_phys) + PAGE_SIZE;
    kern_tsk->kern_stk_rsp = 0;

    kern_tsk->next = kern_tsk;
    kern_tsk->wait_next = NULL;
    g_head_tsk = kern_tsk;
    g_curr_tsk = kern_tsk;
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
    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(&sched_lock, rflags);

    if (g_head_tsk == NULL)
    {
        SPIN_UNLOCK_IRQRESTORE(&sched_lock, rflags);
        return;
    }

    Task *next_tsk;
    if (g_curr_tsk == NULL)
    {
        next_tsk = g_head_tsk;
    }
    else
    {
        next_tsk = g_curr_tsk->next;
        if (next_tsk == NULL)
        {
            next_tsk = g_head_tsk;
        }

        Task *start_tsk = next_tsk;
        while (next_tsk->state != TASK_READY)
        {
            next_tsk = next_tsk->next;
            if (next_tsk == NULL)
            {
                next_tsk = g_head_tsk;
            }
            if (next_tsk == start_tsk)
            {
                next_tsk = g_head_tsk;
                break;
            }
        }
    }

    if (next_tsk == NULL || next_tsk == g_curr_tsk)
    {
        SPIN_UNLOCK_IRQRESTORE(&sched_lock, rflags);
        if (g_curr_tsk != NULL && g_curr_tsk->pid != 0 && (g_curr_tsk->pending_signals & (1 << (SIGINT - 1))))
        {
            g_curr_tsk->pending_signals &= ~(1 << (SIGINT - 1));
            sched_exit(0);
        }
        return;
    }

    Task *prev_tsk = g_curr_tsk;
    g_curr_tsk = next_tsk;

    tss_set_stack(next_tsk->kern_stk_top);
    kern_stk_ptr = next_tsk->kern_stk_top;
    write_cr3(next_tsk->pml4);

    uint8_t *prev_fpu = (prev_tsk != NULL) ? get_aligned_fpu_region(prev_tsk) : NULL;
    uint8_t *next_fpu = get_aligned_fpu_region(next_tsk);

    spinlock_release(&sched_lock);

    if (prev_tsk == NULL)
    {
        switch_to_task(NULL, next_tsk->kern_stk_rsp, NULL, next_fpu);
    }
    else
    {
        switch_to_task(&prev_tsk->kern_stk_rsp, next_tsk->kern_stk_rsp, prev_fpu, next_fpu);
    }

    if (rflags & (1 << 9))
    {
        sti();
    }

    if (g_curr_tsk->pid != 0 && (g_curr_tsk->pending_signals & (1 << (SIGINT - 1))))
    {
        kprint("Signal SIGINT detected! Killing process...\n");
        g_curr_tsk->pending_signals &= ~(1 << (SIGINT - 1));
        sched_exit(0);
    }
}

void sched_block(void)
{
    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(&sched_lock, rflags);
    g_curr_tsk->state = TASK_WAITING;
    SPIN_UNLOCK_IRQRESTORE(&sched_lock, rflags);

    schedule();
}

void sched_wake_pid(int pid)
{
    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(&sched_lock, rflags);

    Task *t = sched_find_task_locked(pid);
    if (t != NULL && t->state != TASK_ZOMBIE && t->state != TASK_DEAD)
    {
        t->state = TASK_READY;
    }

    SPIN_UNLOCK_IRQRESTORE(&sched_lock, rflags);
}

void sched_exit(int code)
{
    if (g_curr_tsk->pid == 0)
    {
        kprint("Kernel cannot exit!\n");
        return;
    }

    Task *task_to_exit = g_curr_tsk;

    sched_clean_gui(task_to_exit);
    sched_clean_fds(task_to_exit);

    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(&sched_lock, rflags);
    (void)rflags;

    sched_reparent_children_locked(task_to_exit);

    Task *next_tsk = task_to_exit->next;
    while (next_tsk->state != TASK_READY)
    {
        next_tsk = next_tsk->next;
        if (next_tsk == task_to_exit)
        {
            next_tsk = g_head_tsk;
            break;
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

    task_to_exit->state = TASK_ZOMBIE;
    task_to_exit->ret_val = code;

    if (task_to_exit->parent != NULL)
    {
        Task *parent = task_to_exit->parent;
        if (parent->state != TASK_DEAD && parent->state != TASK_ZOMBIE)
        {
            parent->state = TASK_READY;
        }
    }

    uint8_t *next_fpu = get_aligned_fpu_region(next_tsk);

    spinlock_release(&sched_lock);
    switch_to_task(NULL, next_tsk->kern_stk_rsp, NULL, next_fpu);
}

void sched_kill(int pid)
{
    if (pid <= 0)
    {
        return;
    }
    if (pid == g_curr_tsk->pid)
    {
        sched_exit(-1);
        return;
    }

    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(&sched_lock, rflags);

    Task *tgt_tsk = sched_find_task_locked(pid);
    if (tgt_tsk == NULL || tgt_tsk->state == TASK_ZOMBIE || tgt_tsk->state == TASK_DEAD)
    {
        SPIN_UNLOCK_IRQRESTORE(&sched_lock, rflags);
        return;
    }

    sched_reparent_children_locked(tgt_tsk);
    tgt_tsk->state = TASK_ZOMBIE;
    tgt_tsk->ret_val = -1;

    if (tgt_tsk->parent != NULL)
    {
        Task *parent = tgt_tsk->parent;
        if (parent->state != TASK_DEAD && parent->state != TASK_ZOMBIE)
        {
            parent->state = TASK_READY;
        }
    }

    SPIN_UNLOCK_IRQRESTORE(&sched_lock, rflags);

    sched_clean_gui(tgt_tsk);
    sched_clean_fds(tgt_tsk);
}

Task *get_curr_task(void)
{
    return g_curr_tsk;
}

Task *sched_find_task(int pid)
{
    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(&sched_lock, rflags);
    Task *t = sched_find_task_locked(pid);
    SPIN_UNLOCK_IRQRESTORE(&sched_lock, rflags);
    return t;
}

int64_t get_curr_task_pid(void)
{
    Task *t = get_curr_task();
    if (t == NULL)
        return -1;
    return t->pid;
}

void sched_send_signal(int pid, uint32_t sig_code)
{
    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(&sched_lock, rflags);
    Task *tsk = sched_find_task_locked(pid);
    if (tsk != NULL)
    {
        tsk->pending_signals |= 1 << (sig_code - 1);
    }
    else
    {
        kprint("SCHED_SEND_SIGNAL failed: No task found for the id\n");
    }
    SPIN_UNLOCK_IRQRESTORE(&sched_lock, rflags);
}

void sched_register_task(Task *tsk)
{
    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(&sched_lock, rflags);

    if (g_head_tsk == NULL)
    {
        g_head_tsk = tsk;
        tsk->next = tsk;
        g_curr_tsk = tsk;
    }
    else
    {
        tsk->next = g_head_tsk->next;
        g_head_tsk->next = tsk;
    }
    tsk->state = TASK_READY;

    SPIN_UNLOCK_IRQRESTORE(&sched_lock, rflags);
}

Task *task_factory_create(uint64_t entry, uint64_t rsp)
{
    Task *tsk = sched_new_task();
    if (tsk == NULL)
        return NULL;
    task_context_setup(tsk, entry, rsp);
    return tsk;
}

Task *task_factory_fork(Task *parent_tsk)
{
    Task *child_tsk = sched_new_task();
    if (child_tsk == NULL)
        return NULL;

    for (int i = 0; i < MAX_OPEN_FILES; i++)
    {
        if (child_tsk->fd_tbl[i] != NULL)
        {
            vfs_close(child_tsk->fd_tbl[i]);
            child_tsk->fd_tbl[i] = NULL;
        }
    }

    asm volatile("fxsave %0" : "=m"(*get_aligned_fpu_region(parent_tsk)));
    uint8_t *parent_fpu = get_aligned_fpu_region(parent_tsk);
    uint8_t *child_fpu = get_aligned_fpu_region(child_tsk);
    memcpy(child_fpu, parent_fpu, 512);

    // Free the default empty PML4 allocated by sched_new_task before replacing it
    if (child_tsk->pml4 != 0)
    {
        vmm_ret_pml4(child_tsk->pml4);
        child_tsk->pml4 = 0;
    }

    uint64_t new_pml4 = vmm_copy_hierarchy(vmm_phys_to_hhdm(parent_tsk->pml4), 4);
    if (new_pml4 == 0)
    {
        sched_destroy_task(child_tsk);
        return NULL;
    }
    child_tsk->pml4 = new_pml4;

    uint64_t kern_stk = pmm_alloc_frame();
    if (kern_stk == 0)
    {
        sched_destroy_task(child_tsk);
        return NULL;
    }
    child_tsk->kern_stk_top = (uint64_t)vmm_phys_to_hhdm(kern_stk) + PAGE_SIZE;

    memcpy(child_tsk->fd_tbl, parent_tsk->fd_tbl, MAX_OPEN_FILES * sizeof(file_handle_t *));
    for (int i = 0; i < MAX_OPEN_FILES; i++)
    {
        if (child_tsk->fd_tbl[i] == NULL)
            continue;
        vfs_retain(child_tsk->fd_tbl[i]);
    }
    memcpy(child_tsk->cwd, parent_tsk->cwd, MAX_CWD_LEN);
    memcpy(child_tsk->name, parent_tsk->name, 64);
    child_tsk->heap_end = parent_tsk->heap_end;
    child_tsk->parent = parent_tsk;
    child_tsk->state = TASK_READY;
    child_tsk->win = parent_tsk->win;

    vmm_cleanup_task(child_tsk);
    child_tsk->vm_alloc_head = vmm_copy_alloc_list(parent_tsk->vm_alloc_head);
    child_tsk->vm_free_head = vmm_copy_free_list(parent_tsk->vm_free_head);

    uint64_t *parent_sp = (uint64_t *)parent_tsk->kern_stk_top - 13;
    uint64_t *child_sp = (uint64_t *)child_tsk->kern_stk_top;

    *(--child_sp) = NEW_TASK_SS;
    uint64_t parent_user_rsp = *(parent_sp + 12);
    *(--child_sp) = parent_user_rsp;
    *(--child_sp) = (uint64_t)*(parent_sp + 10) | 0x200;
    *(--child_sp) = NEW_TASK_CS;
    *(--child_sp) = (uint64_t)*(parent_sp + 11);

    *(--child_sp) = 0;                           // rax = 0 in child
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

    for (int i = 0; i < 6; i++)
    {
        *(--child_sp) = 0;
    }
    child_tsk->kern_stk_rsp = (uint64_t)child_sp;

    return child_tsk;
}

inline static void sched_clean_gui(Task *tsk)
{
    if (tsk->win != NULL)
    {
        if (tsk->win->owner_pid == tsk->pid)
        {
            win_close(tsk->win);
        }
        tsk->win = NULL;
    }
}

inline static void sched_clean_fds(Task *tsk)
{
    for (int i = 0; i < MAX_OPEN_FILES; i++)
    {
        if (tsk->fd_tbl[i] != NULL)
        {
            vfs_close(tsk->fd_tbl[i]);
            tsk->fd_tbl[i] = NULL;
        }
    }
}

void sched_check_sleeping_tasks(void)
{
    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(&sched_lock, rflags);

    if (g_head_tsk == NULL)
    {
        SPIN_UNLOCK_IRQRESTORE(&sched_lock, rflags);
        return;
    }

    int64_t curr_tick = timer_get_ticks();
    Task *t = g_head_tsk;
    do
    {
        if (t->state == TASK_SLEEPING && curr_tick >= t->wake_tick)
        {
            t->state = TASK_READY;
            t->wake_tick = -1;
        }
        t = t->next;
    } while (t != g_head_tsk);

    SPIN_UNLOCK_IRQRESTORE(&sched_lock, rflags);
}

void sched_reap_zombies(void)
{
    Task *zombie_to_reap = NULL;

    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(&sched_lock, rflags);

    if (g_head_tsk != NULL)
    {
        Task *t = g_head_tsk;
        Task *prev = g_head_tsk;
        while (prev->next != g_head_tsk)
        {
            prev = prev->next;
        }

        do
        {
            Task *next = t->next;
            if (t->pid != 0 && t != g_curr_tsk && t->state == TASK_ZOMBIE &&
                (t->parent == NULL || t->parent->pid == 0 || t->parent->state == TASK_ZOMBIE || t->parent->state == TASK_DEAD))
            {
                prev->next = next;
                if (t == g_head_tsk)
                {
                    g_head_tsk = next;
                }
                zombie_to_reap = t;
                break;
            }
            prev = t;
            t = next;
        } while (t != g_head_tsk);
    }

    SPIN_UNLOCK_IRQRESTORE(&sched_lock, rflags);

    if (zombie_to_reap != NULL)
    {
        sched_destroy_task(zombie_to_reap);
    }
}

int sched_get_procs(ProcInfo_t *out_buf, int max_count)
{
    if (out_buf == NULL || max_count <= 0)
    {
        return -1;
    }

    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(&sched_lock, rflags);

    if (g_head_tsk == NULL)
    {
        SPIN_UNLOCK_IRQRESTORE(&sched_lock, rflags);
        return 0;
    }

    int count = 0;
    Task *t = g_head_tsk;
    do
    {
        if (count >= max_count)
        {
            break;
        }
        ProcInfo_t *info = &out_buf[count];
        memset(info, 0, sizeof(ProcInfo_t));
        info->pid = t->pid;
        info->ppid = (t->parent != NULL) ? t->parent->pid : 0;
        info->state = t->state;
        info->heap_kb = (t->pid > 0 && t->heap_end > USER_HEAP_START)
                            ? ((t->heap_end - USER_HEAP_START + 1023) / 1024)
                            : 0;
        info->has_win = (t->win != NULL && t->win->owner_pid == t->pid) ? 1 : 0;
        strncpy(info->name, t->name[0] ? t->name : "task", 63);
        info->name[63] = '\0';
        strncpy(info->cwd, t->cwd[0] ? t->cwd : "/", 63);
        info->cwd[63] = '\0';

        count++;
        t = t->next;
    } while (t != g_head_tsk);

    SPIN_UNLOCK_IRQRESTORE(&sched_lock, rflags);
    return count;
}