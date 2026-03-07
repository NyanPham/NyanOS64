#include "syscall.h"
#include "cpu.h"
#include "gdt.h"
#include "../io.h"
#include "drivers/video.h"
#include "drivers/keyboard.h"
#include "drivers/serial.h" // debugging
#include "drivers/rtc.h"
#include "drivers/timer.h"
#include "fs/tar.h"
#include "fs/pipe.h"
#include "fs/vfs.h"
#include "./string.h"
#include "elf.h"
#include "sched/sched.h"
#include "mem/kmalloc.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "gui/window.h"
#include "kern_defs.h"
#include "include/syscall_args.h"
#include "include/stat.h"
#include "include/signal.h"
#include "utils/asm_instrs.h"
#include "ipc/shm.h"
#include "ipc/mq.h"
#include "event/event.h"
#include "include/syscall_nums.h"

#include <stddef.h>
#include <stdbool.h>

#define UNUSED(x) (void)(x)

#define IA32_EFER 0xC0000080  // a register that allows enabling the SYSCALL/SYSRET instruction (Extended Feature Enable Register)
#define IA32_STAR 0xC0000081  // a register that stores segment selectors for fast system calls, mostly for SYSCALL
#define IA32_LSTAR 0xC0000082 // a register to hold the 64-bit virt_addr of the syscall handler
#define IA32_FMASK 0xC0000084 // a register that masks (clears) specific flags in RFLAGS during a syscall
#define EFER_SCE 1
#define INT_FLAGS 0x200
#define REBOOT_PORT 0x64
#define SHUTDOWN_PORT 0x604

extern void syscall_entry(void);
int8_t find_free_fd(Task *task);
extern void switch_to_task(uint64_t *prev_rsp_ptr, uint64_t next_rsp, uint8_t *prev_fpu, uint8_t *next_fpu);

static bool verify_usr_access(uint64_t ptr, uint64_t size)
{
    uint64_t res = (ptr + size);
    return (res >= ptr && res >= size && res <= KERN_BASE);
}

// static uint64_t sys_reserved(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
// {
//     UNUSED(arg1);
//     UNUSED(arg2);
//     UNUSED(arg3);
//     UNUSED(arg4);
//     UNUSED(arg5);
//     kprint("SYSCALL RESERVED");
//     return 0;
// }

void syscall_init(void)
{
    // first, we enable the Sys call extension
    uint64_t efer = rdmsr(IA32_EFER);
    efer |= EFER_SCE;
    wrmsr(IA32_EFER, efer);

    // set the handler
    wrmsr(IA32_LSTAR, (uint64_t)syscall_entry);

    // entering syscall, we disable all interrupts
    wrmsr(IA32_FMASK, INT_FLAGS);

    /*
    Notes:
    User data must preceed user code.
    Our user data is at offset 0x28, and user code
    is at 0x30. However, sysret auto-cals the user code offset
    by user_data_off + 0x10. That means, sysret thinks
    the user code starts at off 0x38. Oops, we have 8 bytes
    of mismatch. We deliberately tells sysret the user data base
    is 0x20 instead.
    */
    uint64_t kern_sel = GDT_OFFSET_KERNEL_CODE;
    uint64_t usr_base = GDT_OFFSET_USER_DATA - 0x8;
    uint64_t star_val = (usr_base << 0x30) | (kern_sel << 0x20);
    wrmsr(IA32_STAR, star_val);

    kprint("Syscall MSRs configured.\n");
}

static uint64_t sys_read(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg4);
    UNUSED(arg5);
    int8_t fd = (int8_t)arg1;
    char *buf = (char *)arg2;
    uint64_t count = arg3;

    // validate fd
    if (fd < 0 || fd >= MAX_OPEN_FILES)
    {
        return -1;
    }

    Task *curr_tsk = get_curr_task();
    if (curr_tsk == NULL || curr_tsk->fd_tbl[fd] == NULL)
    {
        return -1;
    }

    if (!verify_usr_access((uint64_t)buf, count))
    {
        kprint("SYS_READ: invalid buffer\n");
        return -1;
    }

    // call vfs (if fd > 2, we read the stdin_read)
    return vfs_read(curr_tsk->fd_tbl[fd], count, (uint8_t *)buf);
}

static uint64_t sys_write(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg4);
    UNUSED(arg5);
    int8_t fd = (int8_t)arg1;
    char *buf = (char *)arg2;
    uint64_t count = arg3;

    // validate fd
    if (fd < 0 || fd >= MAX_OPEN_FILES)
    {
        return -1;
    }

    Task *curr_tsk = get_curr_task();
    if (curr_tsk == NULL || curr_tsk->fd_tbl[fd] == NULL)
    {
        return -1;
    }

    if (!verify_usr_access((uint64_t)buf, count))
    {
        kprint("SYS_WRITE: invalid buffer\n");
        return -1;
    }

    // call vfs (if fd == 1, it calls the stdout_write)
    return vfs_write(curr_tsk->fd_tbl[fd], count, (uint8_t *)buf);
}

static uint64_t sys_shutdown(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg1);
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);

    cli();

    char *msg = "NyanOS is shutting down. Goodbye!";
    video_clear();

    uint64_t scrn_w = video_get_width();
    uint64_t scrn_h = video_get_height();

    video_add_dirty_rect(0, 0, scrn_w, scrn_h);

    kprint("========================================\n");
    kprint("        NyanOS64 is shutting down       \n");
    kprint("========================================\n");

    int text_width = strlen(msg) * CHAR_W;
    int text_height = CHAR_H;

    int x = (scrn_w / 2) - (text_width / 2);
    int y = (scrn_h / 2) - (text_height / 2);

    video_draw_string(x, y, msg, White);

    video_swap();

    kprint("[INFO] Terminating all user processes...\n");
    // TODO: Kill all process

    kprint("[INFO] Syncing disks to prevent data loss...\n");
    // TODO: vfs_sync() or fat32_flush_cache()

    kprint("[INFO] Powering off. See you next time, Creator!\n");

    for (volatile int i = 0; i < 3000000; i++)
    {
        pause();
    }

    outw(0x604, 0x2000);  // QEMU
    outw(0xB004, 0x2000); // Bochs/VirtualBox

    cli();
    hlt();
    return 0;
}
static uint64_t sys_clear(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg1);
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    video_clear();
    return 0;
}

static uint64_t sys_reboot(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg1);
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    outb(REBOOT_PORT, 0xFE);
    return 0;
}

static uint64_t sys_list_files(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    tar_list((char *)arg1, arg2);
    return 0;
}

static uint64_t sys_fork(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg1);
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    Task *parent_tsk = get_curr_task();
    if (parent_tsk == NULL || parent_tsk->pml4 == 0)
    {
        return -1;
    }
    Task *child_tsk = task_factory_fork(parent_tsk);
    sched_register_task(child_tsk);
    return child_tsk->pid; // for parent, it fork returns child's pid
}

static uint64_t sys_exec(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    char *raw_path = (char *)arg1;
    char **argv = (char **)arg2;

    int argc = 0;
    size_t argv_size = 0;
    while (argv[argc] != NULL)
    {
        argv_size += strlen(argv[argc]) + 1;
        argc++;
    }

    size_t argv_ptr_size = (argc + 1) * sizeof(uint64_t);

    char *argv_buf = kmalloc(argv_size);

    int cpy_off = 0;
    for (int i = 0; i < argc; i++)
    {
        size_t len = strlen(argv[i]) + 1;
        memcpy(&argv_buf[cpy_off], argv[i], len);
        cpy_off += len;
    }

    uint64_t start_usr_addr = (USER_STACK_TOP - argv_size) & ~0xF; // address to store the argv content in the new stack
    uint64_t tentative_list_addr = start_usr_addr - argv_ptr_size;

    if ((tentative_list_addr & 0xF) == 0)
    {
        start_usr_addr -= 8;
    }

    uint64_t *argv_list = (uint64_t *)kmalloc(argv_ptr_size);

    size_t curr_off = 0;
    for (int i = 0; i < argc; i++)
    {
        argv_list[i] = start_usr_addr + curr_off;
        curr_off += strlen(argv[i]) + 1;
    }

    argv_list[argc] = 0;

    // handle the path
    Task *curr_tsk = get_curr_task();
    char full_path[256];
    resolve_path(curr_tsk->cwd, raw_path, full_path);

    char *elf_fname = kmalloc(strlen(full_path) + 1);
    strcpy(elf_fname, full_path);

    uint64_t old_pml4 = read_cr3();

    uint64_t new_pml4 = vmm_new_pml4(); // copy kernel space, while the user space is empty
    if (new_pml4 == 0)
    {
        kfree(argv_buf);
        kfree(argv_list);
        return -1;
    }

    /*
    turn of the interrupts to
    avoid our scheduler intervention
    when the pml4 swapping is still happening
    */
    cli();
    write_cr3(new_pml4);
    curr_tsk->pml4 = new_pml4;
    sti();

    uint64_t virt_usr_stk_base = USER_STACK_TOP - (PAGE_SIZE * USER_STACK_PAGES);
    uint64_t *new_pml4_hhdm = vmm_phys_to_hhdm(new_pml4);

    for (int i = 0; i < USER_STACK_PAGES; i++)
    {
        uint64_t phys_usr_stk_frame = pmm_alloc_frame();

        vmm_map_page(
            new_pml4_hhdm,
            virt_usr_stk_base + (PAGE_SIZE * i),
            phys_usr_stk_frame,
            VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
    }

    uint64_t phys_usr_stk = vmm_virt2phys(new_pml4_hhdm, start_usr_addr) & ~0xFFF;

    // copy the argv_buf
    uint64_t offset_buf = start_usr_addr & 0xFFF;
    void *dest_buf = vmm_phys_to_hhdm(phys_usr_stk + offset_buf);
    memcpy(dest_buf, argv_buf, argv_size);

    // copy the argv_list
    uint64_t start_list_addr = start_usr_addr - argv_ptr_size;
    uint64_t offset_list = start_list_addr & 0xFFF;
    void *dest_list = vmm_phys_to_hhdm(phys_usr_stk + offset_list);
    memcpy(dest_list, argv_list, argv_ptr_size);

    // write argc into the rsp top
    // virt_rsp = start_list_addr - 8
    uint64_t rsp_virt_addr = start_list_addr - sizeof(uint64_t);
    uint64_t offset_rsp = rsp_virt_addr & 0xFFF;
    uint64_t *dest_rsp = vmm_phys_to_hhdm(phys_usr_stk + offset_rsp);
    *dest_rsp = argc;

    kfree(argv_buf);
    kfree(argv_list);

    uint64_t entry = elf_load(elf_fname);

    if (entry == 0)
    {
        cli();
        write_cr3(old_pml4);
        curr_tsk->pml4 = old_pml4;
        sti();
        kfree(elf_fname);
        vmm_ret_pml4(new_pml4);
        return -1;
    }

    kfree(elf_fname);

    curr_tsk->heap_end = USER_HEAP_START;

    vmm_cleanup_task(curr_tsk);
    VmFreeRegion *vm_free_head = (VmFreeRegion *)kmalloc(sizeof(VmFreeRegion));
    if (vm_free_head == NULL)
    {
        return -1;
    }
    vm_free_head->addr = USER_MMAP_START;
    vm_free_head->size = USER_MMAP_SIZE;
    vm_free_head->next = NULL;

    curr_tsk->vm_free_head = vm_free_head;
    curr_tsk->vm_alloc_head = NULL;

    vmm_free_table(vmm_phys_to_hhdm(old_pml4), 4);

    if (curr_tsk->win != NULL)
    {
        if (curr_tsk->win->owner_pid == curr_tsk->pid)
        {
            win_close(curr_tsk->win);
        }
        curr_tsk->win = NULL;
    }

    task_context_reset(curr_tsk, entry, rsp_virt_addr);

    uint64_t fpu_addr = (uint64_t)curr_tsk->fpu_regs;
    uint8_t *fpu_aligned = (uint8_t *)((fpu_addr + 15) & ~((uint64_t)0xF));
    memset(fpu_aligned, 0, 512);
    *((uint16_t *)(fpu_aligned)) = 0x037F;
    *((uint32_t *)(fpu_aligned + 0x18)) = 0x1F80;
    switch_to_task(NULL, curr_tsk->kern_stk_rsp, NULL, fpu_aligned);

    return 0;
}

static uint64_t sys_exit(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    sched_exit((int)(arg1));
    return 0;
}

static uint64_t sys_waitpid(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    int pid = (int)arg1;
    int *stat = (int *)arg2;

    Task *child = sched_find_task(pid);
    if (child == NULL)
    {
        return -1;
    }

    while (1)
    {
        if (child->state == TASK_ZOMBIE)
        {
            if (stat != NULL)
            {
                if (!verify_usr_access((uint64_t)stat, sizeof(int)))
                {
                    kprint("SYS_WAITPID: Invalid user access memory\n");
                    return -1;
                }
                *stat = child->ret_val;
            }
            sched_unlink_task(child);
            sched_destroy_task(child);
            return pid;
        }
        else
        {
            sched_block();
        }
    }
}

static uint64_t sys_open(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    char *fname = (char *)arg1;
    uint32_t mode = (uint32_t)arg2;

    if (!verify_usr_access((uint64_t)fname, 1))
    {
        kprint("SYS_OPEN failed: Invalid user access\n");
        return -1;
    }

    Task *curr_tsk = get_curr_task();

    int8_t fd = find_free_fd(curr_tsk);
    if (fd < 0)
    {
        return -1;
    }

    char full_path[256];
    resolve_path(curr_tsk->cwd, fname, full_path);

    file_handle_t *f = vfs_open(full_path, mode);
    if (f == NULL)
    {
        return -1;
    }

    curr_tsk->fd_tbl[fd] = f;
    return fd;
}

static uint64_t sys_close(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    int8_t fd = (int8_t)arg1;
    if (fd < 0 || fd >= MAX_OPEN_FILES)
    {
        return -1;
    }

    Task *curr_tsk = get_curr_task();
    if (curr_tsk->fd_tbl[fd] == NULL)
    {
        return -1;
    }

    vfs_close(curr_tsk->fd_tbl[fd]);
    curr_tsk->fd_tbl[fd] = NULL;
    return 0;
}

static uint64_t sys_sbrk(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    int64_t incr_payload = (int64_t)arg1;
    Task *curr_tsk = get_curr_task();

    uint64_t prev_brk = curr_tsk->heap_end;
    uint64_t next_brk = prev_brk + incr_payload;

    if (incr_payload == 0)
    {
        return prev_brk;
    }

    // TODO: Support the shrink heap
    if (incr_payload < 0)
    {
        return prev_brk;
    }

    // old page: (prev_brk - 1) / PAGE_SIZE
    // new page: (next_brk - 1) / PAGE_SIZE
    // new page > old page, then map more
    uint64_t start_page_addr = (prev_brk % PAGE_SIZE) == 0
                                   ? prev_brk
                                   : (prev_brk + 0xFFF) & ~0xFFF;
    uint64_t end_page_addr = (next_brk + 0xFFF) & ~0xFFF;

    for (uint64_t virt_addr = start_page_addr; virt_addr < end_page_addr; virt_addr += PAGE_SIZE)
    {
        uint64_t phys_addr = pmm_alloc_frame();
        if (phys_addr == 0)
        {
            kprint("SYS_BRK: out of memory!\n");
            return -1;
        }

        void *phys_addr_hhdm = vmm_phys_to_hhdm(phys_addr);
        memset(phys_addr_hhdm, 0, PAGE_SIZE); // Security: Clean the page

        vmm_map_page(
            vmm_phys_to_hhdm(curr_tsk->pml4),
            virt_addr,
            (uint64_t)phys_addr,
            VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
    }
    curr_tsk->heap_end = next_brk;
    return prev_brk;
}

static uint64_t sys_kprint(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    char *s = (char *)arg1;
    if (!verify_usr_access((uint64_t)s, 1))
    {
        kprint("SYS_KPRINT: invalid buffer\n");
        return -1;
    }
    if (!verify_usr_access((uint64_t)s, strlen(s) + 1))
    {
        kprint("SYS_KPRINT: invalid buffer\n");
        return -1;
    }
    kprint(s);
    return 0;
}

static uint64_t sys_get_key(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg1);
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    return (uint64_t)keyboard_get_char();
}

static uint64_t sys_chdir(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    char *path = (char *)arg1;
    if (!verify_usr_access((uint64_t)path, 1))
    {
        return -1;
    }

    Task *curr_tsk = get_curr_task();

    char new_path[256];
    resolve_path(curr_tsk->cwd, path, new_path);
    // TODO: check if the directory exists with new vfs_is_dir(path)
    // now just assumes we always have it

    if (strlen(new_path) >= 255)
    {
        return -1;
    }

    strcpy(curr_tsk->cwd, new_path);
    return 0;
}

static uint64_t sys_getcwd(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    char *buf = (char *)arg1;
    uint64_t size = arg2;

    if (!verify_usr_access((uint64_t)buf, size))
    {
        return -1;
    }

    Task *curr_tsk = get_curr_task();
    uint64_t len = strlen(curr_tsk->cwd);

    if (size < len + 1)
    {
        return -1;
    }

    strcpy(buf, curr_tsk->cwd);
    return 0;
}

static uint64_t sys_create_win(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    WinParams_t *win_params = (WinParams_t *)arg1;

    if (win_params == NULL)
    {
        kprint("Not having window params passed!\n");
        return -1;
    }

    if (!verify_usr_access((uint64_t)win_params, sizeof(WinParams_t)))
    {
        kprint("Invalid WinParams space!\n");
        return -1;
    }

    char *w_title = (char *)kmalloc(256);
    if (w_title != NULL)
    {
        strncpy(w_title, win_params->title, 256);
        w_title[255] = 0;
    }

    Task *tsk = get_curr_task();
    if (tsk->win != NULL)
    {
        kprint("Task already has window, on attempt to create win: ");
        kprint(w_title);
        kprint("\n");

        return -1;
    }

    win_create(win_params->x, win_params->y, win_params->width, win_params->height, w_title, win_params->flags);
    return 0;
}

static uint64_t sys_kprint_int(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    int x = (int)arg1;
    kprint_int(x);
    return 0;
}

static uint64_t sys_mkdir(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    char *path = (char *)arg1;
    uint32_t flags = (uint32_t)arg2;

    Task *curr_tsk = get_curr_task();

    char cwd[256];
    if (curr_tsk->cwd[0] == '\0')
    {
        strcpy(cwd, "/");
    }
    else
    {
        strcpy(cwd, curr_tsk->cwd);
    }

    char full_path[256];
    resolve_path(cwd, path, full_path);

    if (strlen(full_path) >= 255)
    {
        return -1;
    }

    char parent_path[256];
    char new_dir_name[128];

    int last_slash = -1;
    int len = strlen(full_path);

    for (int i = len - 1; i >= 0; i--)
    {
        if (full_path[i] == '/')
        {
            last_slash = i;
            break;
        }
    }

    if (last_slash == -1)
    {
        return -1;
    }

    strncpy(parent_path, full_path, last_slash);
    parent_path[last_slash] = '\0';
    if (last_slash == 0)
    {
        strcpy(parent_path, "/");
    }

    strcpy(new_dir_name, &full_path[last_slash + 1]);

    vfs_node_t *parent_node = vfs_navigate(parent_path);

    if (parent_node != NULL && parent_node->ops != NULL && parent_node->ops->create != NULL)
    {
        vfs_node_t *new_node = parent_node->ops->create(parent_node, new_dir_name, flags);
        vfs_node_t *chk_node = vfs_navigate(full_path);
        if (new_node != NULL && chk_node != NULL)
        {
            return 0;
        }
    }

    return -1;
}

static uint64_t sys_pipe(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    int *fd_ptr = (int *)arg1;

    // Firstly, finds the 2 free spots in the fd_tbl
    Task *curr_tsk = get_curr_task();
    if (curr_tsk == NULL)
    {
        return -1;
    }

    int8_t read_fd = find_free_fd(curr_tsk);
    if (read_fd < 0)
    {
        return -1;
    }
    file_handle_t *handle_read = (file_handle_t *)kmalloc(sizeof(file_handle_t));
    if (handle_read == NULL)
    {
        kprint("SYS_PIPE failed: out of memory\n");
        return -1;
    }
    curr_tsk->fd_tbl[read_fd] = handle_read;

    int8_t write_fd = find_free_fd(curr_tsk);
    if (write_fd < 0)
    {
        curr_tsk->fd_tbl[read_fd] = NULL;
        kfree(handle_read);
        return -1;
    }

    file_handle_t *handle_write = (file_handle_t *)kmalloc(sizeof(file_handle_t));
    if (handle_write == NULL)
    {
        kprint("SYS_PIPE failed: out of memory\n");
        curr_tsk->fd_tbl[read_fd] = NULL;
        kfree(handle_read);
        return -1;
    }

    curr_tsk->fd_tbl[write_fd] = handle_write;

    Pipe *pipe = (Pipe *)kmalloc(sizeof(Pipe));

    if (pipe == NULL)
    {
        // kfree already checks if the ptr is NULL, so it's consise to kfree 3
        kprint("SYS_PIPE failed: out of memory\n");
        kfree(handle_read);
        kfree(handle_write);
        curr_tsk->fd_tbl[read_fd] = NULL;
        curr_tsk->fd_tbl[write_fd] = NULL;
        return -1;
    }

    // Set up Pipe
    rb_init(&pipe->buf);
    pipe->reader_pid = -1;
    pipe->writer_pid = -1;
    pipe->flags = READ_OPEN | WRITE_OPEN;

    vfs_node_t *node_read = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (node_read == NULL)
    {
        kprint("SYS_PIPE failed: out of memory\n");
        kfree(pipe);
        kfree(handle_read);
        kfree(handle_write);
        curr_tsk->fd_tbl[read_fd] = NULL;
        curr_tsk->fd_tbl[write_fd] = NULL;
        return -1;
    }

    vfs_node_t *node_write = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (node_write == NULL)
    {
        kprint("SYS_PIPE failed: out of memory\n");
        kfree(pipe);
        kfree(handle_read);
        kfree(handle_write);
        kfree(node_read);
        curr_tsk->fd_tbl[read_fd] = NULL;
        curr_tsk->fd_tbl[write_fd] = NULL;
        return -1;
    }

    // Set up Read End
    strcpy(node_read->name, "pipe_read_end");
    node_read->flags = VFS_CHAR_DEVICE | VFS_NODE_AUTOFREE;
    node_read->length = 0;
    node_read->device_data = pipe;
    node_read->ops = &pipe_read_ops;

    // Set up Write End
    strcpy(node_write->name, "pipe_write_end");
    node_write->flags = VFS_CHAR_DEVICE | VFS_NODE_AUTOFREE;
    node_write->length = 0;
    node_write->device_data = pipe;
    node_write->ops = &pipe_write_ops;

    // Setup handles
    handle_read->node = node_read;
    handle_read->mode = 1; // any works
    handle_read->offset = 0;
    handle_read->ref_count = 1;

    handle_write->node = node_write;
    handle_write->mode = 2; // any works
    handle_write->offset = 0;
    handle_write->ref_count = 1;

    fd_ptr[0] = read_fd;
    fd_ptr[1] = write_fd;

    return 0;
}

static uint64_t sys_dup2(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    int old_fd = (int)arg1;
    int new_fd = (int)arg2;

    Task *curr_tsk = get_curr_task();
    if (curr_tsk == NULL)
    {
        return -1;
    }

    if (old_fd < 0 || old_fd >= MAX_OPEN_FILES || new_fd < 0 || new_fd >= MAX_OPEN_FILES)
    {
        return -1;
    }

    if (curr_tsk->fd_tbl[old_fd] == NULL)
    {
        return -1;
    }

    if (old_fd == new_fd)
    {
        return new_fd;
    }

    if (curr_tsk->fd_tbl[new_fd] != NULL)
    {
        vfs_close(curr_tsk->fd_tbl[new_fd]);
        curr_tsk->fd_tbl[new_fd] = NULL;
    }

    curr_tsk->fd_tbl[new_fd] = curr_tsk->fd_tbl[old_fd];
    vfs_retain(curr_tsk->fd_tbl[new_fd]);

    return new_fd;
}

static uint64_t sys_getpid(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg1);
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    return get_curr_task_pid();
}

static uint64_t sys_readdir(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg4);
    UNUSED(arg5);
    int fd = (int)arg1;
    uint32_t idx = (uint32_t)arg2;
    dirent_t *user_dirent = (dirent_t *)arg3;

    if (fd < 0 || fd >= MAX_OPEN_FILES)
    {
        return -1;
    }

    Task *curr_tsk = get_curr_task();
    if (curr_tsk->fd_tbl[fd] == NULL)
    {
        return -1;
    }

    file_handle_t *fh = curr_tsk->fd_tbl[fd];
    if (fh == NULL)
    {
        return -1;
    }

    if (!verify_usr_access((uint64_t)user_dirent, sizeof(dirent_t)))
    {
        kprint("SYS_READDIR: Invalid user ptr\n");
        return -1;
    }

    return vfs_readdir(fh->node, idx, user_dirent);
}

static uint64_t sys_unlink(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    char *path = (char *)arg1;
    if (!verify_usr_access((uint64_t)path, 1))
    {
        return -1;
    }

    Task *curr_tsk = get_curr_task();

    char cwd[256];
    if (curr_tsk->cwd[0] == '\0')
    {
        strcpy(cwd, "/");
    }
    else
    {
        strcpy(cwd, curr_tsk->cwd);
    }

    char new_path[256];
    resolve_path(cwd, path, new_path);

    if (strlen(new_path) >= 255)
    {
        return -1;
    }

    return vfs_unlink(new_path);
}

static uint64_t sys_shm_open(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg4);
    UNUSED(arg5);
    const char *name = (const char *)arg1;
    int flags = (int)arg2;
    int mode = (int)arg3;

    if (!verify_usr_access(arg1, 64))
    {
        kprint("SYS_SHM_OPEN: invalid name address space\n");
        return -1;
    }

    vfs_node_t *node = shm_create_vfs_node(name, flags);

    file_handle_t *handle = (file_handle_t *)kmalloc(sizeof(file_handle_t));
    if (handle == NULL)
    {
        kprint("SYS_SHM_OPEN failed: OOM\n");
        kfree(node);
        ((SharedMem_t *)(node->device_data))->ref_count--;
        return -1;
    }

    handle->node = node;
    handle->offset = 0;
    handle->mode = mode;
    handle->ref_count = 1;

    Task *curr_tsk = get_curr_task();

    int8_t fd = find_free_fd(curr_tsk);
    if (fd < 0)
    {
        return -1;
    }

    curr_tsk->fd_tbl[fd] = handle;

    return fd;
}

static uint64_t sys_ftruncate(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    int fd = (int)arg1;
    uint64_t length = arg2;

    Task *curr_tsk = get_curr_task();
    file_handle_t *fhandle = curr_tsk->fd_tbl[fd];
    if (fhandle == NULL || fhandle->node == NULL || fhandle->node->ops != &shm_ops)
    {
        kprint("SYS_FTRUNCATE: invalid fd or fd is not an SHM\n");
        return -1;
    }

    SharedMem_t *shm = (SharedMem_t *)fhandle->node->device_data;
    if (shm == NULL)
    {
        kprint("SYS_FTRUNCATE: invalid fd\n");
        return -1;
    }

    return shm_set_size(shm, (uint32_t)length);
}

static uint64_t sys_mmap(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg1);
    UNUSED(arg3);
    UNUSED(arg4);
    // we'll skipp addr, offset and prot
    uint64_t length = arg2;
    int fd = (int)arg5;

    if (fd < 0 || fd >= MAX_OPEN_FILES)
    {
        kprint("Invalid FD range\n");
        return 0;
    }

    Task *curr_tsk = get_curr_task();
    file_handle_t *fhandle = curr_tsk->fd_tbl[fd];
    if (fhandle == NULL || fhandle->node == NULL || fhandle->node->ops != &shm_ops)
    {
        return 0;
    }

    SharedMem_t *shm = (SharedMem_t *)fhandle->node->device_data;
    if (shm == NULL || shm->page_count == 0)
    {
        kprint("SYS_MAP failed: SHM struct null or page_count=0\n");
        return 0;
    }

    // find free addresses from virtual space
    // which is large enough
    VmFreeRegion **free_head;
    VmAllocatedList **alloc_head;

    // could've called get `get_vm_ctx(&free_head, &alloc_head);`
    // but we already have curr_tsk, save a bit of cycles :)))
    free_head = &curr_tsk->vm_free_head;
    alloc_head = &curr_tsk->vm_alloc_head;

    size_t aligned_len = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t virt_start_addr = find_free_addr(free_head, aligned_len);

    if (virt_start_addr == 0)
    {
        kprint("SYS_MMAP failed: OOM\n");
        return 0;
    }

    // manually map by unrolling inconsecutive phys pages
    // to consecutive virt addrs
    uint64_t *pml4 = vmm_phys_to_hhdm(read_cr3());

    for (uint32_t i = 0; i < shm->page_count; i++)
    {
        uint64_t phys_addr = shm->phys_pages[i];
        uint64_t virt_addr = virt_start_addr + i * PAGE_SIZE;
        vmm_map_page(
            pml4,
            virt_addr,
            phys_addr,
            VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
    }

    vmm_add_allocated_mem(alloc_head, virt_start_addr, aligned_len, VMM_FLAG_SHM);

    return virt_start_addr;
}

static uint64_t sys_munmap(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    void *addr = (void *)arg1;

    if (!verify_usr_access((uint64_t)addr, 1))
    {
        return -1;
    }

    vmm_free(addr);

    return 0;
}

static uint64_t sys_fstat(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    int fd = (int)arg1;
    stat_t *st = (stat_t *)arg2;

    if (fd < 0 || fd >= MAX_OPEN_FILES)
    {
        kprint("SYS_FSTAT failed: invalid fd\n");
        return -1;
    }

    Task *curr_tsk = get_curr_task();
    file_handle_t *fh = curr_tsk->fd_tbl[fd];

    if (fh == NULL || fh->node == NULL)
    {
        kprint("SYS_FSTAT failed: invalid file handle\n");
        return -1;
    }

    if (!verify_usr_access((uint64_t)st, sizeof(stat_t)))
    {
        kprint("SYS_FSTAT failed: invalid stat buffer\n");
        return -1;
    }

    memset(st, 0, sizeof(stat_t));

    if (fh->node->ops == &shm_ops)
    {
        // Is SharedMem
        SharedMem_t *shm = (SharedMem_t *)fh->node->device_data;
        if (shm)
        {
            st->st_size = shm->size;
        }
        else
        {
            st->st_size = 0;
        }
        st->st_mode = S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    }
    else if (fh->node->flags == VFS_DIRECTORY)
    {
        // Is Directory
        st->st_size = 0;
        st->st_mode = S_IFDIR;
    }
    else
    {
        // normal file
        st->st_size = fh->node->length;
        st->st_mode = S_IFREG;
    }

    st->st_ino = (uint64_t)fh->node;

    return 0;
}

static uint64_t sys_mq_open(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    char *name = (char *)arg1;
    int flags = (int)arg2;

    if (!verify_usr_access((uint64_t)name, 1))
    {
        return 0;
    }

    return (uint64_t)mq_open(name, flags);
}

static uint64_t sys_mq_send(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg4);
    UNUSED(arg5);
    MessageQueue_t *mq = (MessageQueue_t *)arg1;
    void *data = (void *)arg2;
    size_t size = (size_t)arg3;

    if (mq == NULL)
    {
        return -1;
    }

    if (!verify_usr_access((uint64_t)data, size))
    {
        return -1;
    }

    return mq_send(mq, data, size);
}

static uint64_t sys_mq_receive(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg4);
    UNUSED(arg5);
    MessageQueue_t *mq = (MessageQueue_t *)arg1;
    void *buf = (void *)arg2;
    size_t len = (size_t)arg3;

    if (mq == NULL)
    {
        return -1;
    }

    if (!verify_usr_access((uint64_t)buf, len))
    {
        return -1;
    }

    return mq_receive(mq, buf, len);
}

static uint64_t sys_mq_unlink(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    char *name = (char *)arg1;

    if (!verify_usr_access((uint64_t)name, 1))
    {
        return -1;
    }

    return mq_unlink(name);
}

static uint64_t sys_get_time(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    Time_t *u_time = (Time_t *)arg1;

    if (!verify_usr_access((uint64_t)u_time, sizeof(Time_t)))
    {
        kprint("SYS_GET_TIME: Invalid memory access\n");
        return -1;
    }

    Time_t *k_time = rtc_get_time();
    if (k_time == NULL)
    {
        return -1;
    }

    memcpy(u_time, k_time, sizeof(Time_t));
    kfree(k_time);

    return 0;
}

static uint64_t sys_draw_rect(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    int x = (int)arg1;
    int y = (int)arg2;
    int w = (int)arg3;
    int h = (int)arg4;
    uint32_t color = (uint32_t)arg5;

    Task *curr_tsk = get_curr_task();
    if (curr_tsk->win == NULL)
    {
        return -1;
    }

    win_fill_rect(curr_tsk->win, x, y, w, h, color);

    return 0;
}

static uint64_t sys_sleep(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    uint64_t ms = arg1;

    Task *curr_tsk = get_curr_task();

    uint64_t ticks_to_wait = ms / 10;
    if (ticks_to_wait == 0)
    {
        ticks_to_wait = 1;
    }

    curr_tsk->wake_tick = timer_get_ticks() + (int64_t)ticks_to_wait;
    curr_tsk->state = TASK_SLEEPING;

    schedule();

    return 0;
}

static uint64_t sys_blit(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    int x = (int)arg1;
    int y = (int)arg2;
    int w = (int)arg3;
    int h = (int)arg4;
    uint32_t *buf = (uint32_t *)arg5;

    Task *curr_tsk = get_curr_task();
    if (curr_tsk->win == NULL)
    {
        return -1;
    }

    if (!verify_usr_access((uint64_t)buf, w * h * sizeof(uint32_t)))
    {
        kprint("SYS_BLIT: Invalid buffer access\n");
        return -1;
    }

    win_draw_bitmap(curr_tsk->win, x, y, w, h, buf);
    return 0;
}

static uint64_t sys_get_event(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    Event *user_event = (Event *)arg1;
    uint32_t flags = (uint32_t)arg2;

    if (!verify_usr_access((uint64_t)user_event, sizeof(Event)))
    {
        kprint("SYS_GET_EVENT: Invalid memory access\n");
        return -1;
    }

    Task *curr_tsk = get_curr_task();
    Event e;

    while (1)
    {
        if (event_queue_pop(curr_tsk->event_queue, &e) == 1)
        {
            break;
        }

        if (flags & O_NONBLOCK)
        {
            return 0;
        }
        sched_block();
    }

    memcpy(user_event, &e, sizeof(Event));
    return 1;
}

static uint64_t sys_set_fg(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    Task *curr_tsk = get_curr_task();
    if (curr_tsk != NULL)
    {
        curr_tsk->fg_pid = (int)arg1;
    }

    return 0;
}

static uint64_t sys_kill_fg(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg2);
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    int shell_pid = (int)arg1;
    Task *tsk = sched_find_task(shell_pid);

    if (tsk != NULL && tsk->fg_pid != -1)
    {
        sched_send_signal(tsk->fg_pid, SIGINT);
        sched_wake_pid(tsk->fg_pid);
        return 1;
    }
    return 0;
}

static uint64_t sys_await_io(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg5);
    int *fds = (int *)arg1;
    int num_fds = (int)arg2;
    int await_gui = (int)arg3;
    int non_block = (int)arg4;

    Task *curr_tsk = get_curr_task();

    while (1)
    {
        int ready_mask = 0;

        if (await_gui && curr_tsk->event_queue->head != curr_tsk->event_queue->tail)
        {
            ready_mask |= 1;
        }

        for (int i = 0; i < num_fds; i++)
        {
            int fd = fds[i];
            if (fd >= 0 && fd < MAX_OPEN_FILES && curr_tsk->fd_tbl[fd] != NULL)
            {
                file_handle_t *fh = curr_tsk->fd_tbl[fd];
                if (fh->node && fh->node->ops && fh->node->ops->check_ready)
                {
                    if (fh->node->ops->check_ready(fh->node))
                    {
                        ready_mask |= (1 << (i + 1));
                    }
                }
            }
        }

        if (ready_mask != 0)
        {
            return ready_mask;
        }

        if (non_block == 1)
        {
            return 0;
        }
        sched_block();
    }
}

static uint64_t sys_win_get_size(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    UNUSED(arg3);
    UNUSED(arg4);
    UNUSED(arg5);
    int *w = (int *)arg1;
    int *h = (int *)arg2;

    Task *curr_tsk = get_curr_task();
    if (curr_tsk->win)
    {
        win_get_client_size(curr_tsk->win, w, h);
        return 0;
    }
    return -1;
}

typedef uint64_t (*syscall_ptr_t)(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);

static syscall_ptr_t syscall_table[MAX_SYSCALLS] = {
    [SYS_READ] = sys_read,
    [SYS_WRITE] = sys_write,
    [SYS_OPEN] = sys_open,
    [SYS_CLOSE] = sys_close,
    [SYS_MKDIR] = sys_mkdir,
    [SYS_READDIR] = sys_readdir,
    [SYS_UNLINK] = sys_unlink,
    [SYS_CHDIR] = sys_chdir,
    [SYS_GETCWD] = sys_getcwd,
    [SYS_FSTAT] = sys_fstat,
    [SYS_PIPE] = sys_pipe,
    [SYS_DUP2] = sys_dup2,
    [SYS_LIST_FILES] = sys_list_files,
    [SYS_FORK] = sys_fork,
    [SYS_EXEC] = sys_exec,
    [SYS_EXIT] = sys_exit,
    [SYS_WAITPID] = sys_waitpid,
    [SYS_GETPID] = sys_getpid,
    [SYS_SLEEP] = sys_sleep,
    [SYS_SBRK] = sys_sbrk,
    [SYS_MMAP] = sys_mmap,
    [SYS_MUNMAP] = sys_munmap,
    [SYS_FTRUNCATE] = sys_ftruncate,
    [SYS_SHM_OPEN] = sys_shm_open,
    [SYS_MQ_OPEN] = sys_mq_open,
    [SYS_MQ_SEND] = sys_mq_send,
    [SYS_MQ_RECEIVE] = sys_mq_receive,
    [SYS_MQ_UNLINK] = sys_mq_unlink,
    [SYS_CREATE_WIN] = sys_create_win,
    [SYS_WIN_GET_SIZE] = sys_win_get_size,
    [SYS_DRAW_RECT] = sys_draw_rect,
    [SYS_BLIT] = sys_blit,
    [SYS_GET_EVENT] = sys_get_event,
    [SYS_SET_FG] = sys_set_fg,
    [SYS_KILL_FG] = sys_kill_fg,
    [SYS_AWAIT_IO] = sys_await_io,
    [SYS_REBOOT] = sys_reboot,
    [SYS_SHUTDOWN] = sys_shutdown,
    [SYS_GET_TIME] = sys_get_time,
    [SYS_GET_KEY] = sys_get_key,
    [SYS_KPRINT] = sys_kprint,
    [SYS_KPRINT_INT] = sys_kprint_int,
    [SYS_CLEAR] = sys_clear,
};

/**
 * @brief Handles system calls
 *
 * | Syscall # | Function Name    | Description                                |
 * |-----------|------------------|--------------------------------------------|
 * | 0         | sys_read         | Reads from a file descriptor.              |
 * | 1         | sys_write        | Writes to a file descriptor.               |
 * | 2         | -                | Reserved.                                  |
 * | 3         | sys_clear        | Clears the video screen.                   |
 * | 4         | sys_reboot       | Reboots the system.                        |
 * | 5         | sys_list_files   | Lists files in the root directory (TAR).   |
 * | 6         | sys_fork         | Forks the current task.                    |
 * | 7         | sys_exec         | Executes a new program (ELF).              |
 * | 8         | sys_exit         | Terminates the current process.            |
 * | 9         | sys_waitpid      | Waits for a child process to exit.         |
 * | 10        | sys_open         | Opens a file.                              |
 * | 11        | sys_close        | Closes a file descriptor.                  |
 * | 12        | sys_sbrk         | Change program break / allocate user heap  |
 * | 13        | sys_kprint       | Kernel debug print callable from userland  |
 * | 14        | sys_get_key      | Reads a key from the keyboard buffer       |
 * | 15        | sys_chdir        | Change directory                           |
 * | 16        | sys_getcwd       | Get current working directory              |
 * | 13        | sys_kprint       | Kernel debug print callable from userland  |
 * | 14        | sys_get_key      | Reads a key from the keyboard buffer       |
 * | 15        | sys_chdir        | Change directory                           |
 * | 16        | sys_getcwd       | Get current working directory              |
 * | 17        | sys_create_win   | Creates a GUI window                       |
 * | 18        | sys_kprint_int   | Kernel debug print integer                 |
 * | 19        | sys_create_term  | Creates a terminal window                  |
 * | 22        | sys_getpid       | Gets the current process ID                |
 *
 * @param sys_num The system call number.
 * @return The return value of the system call (typically 0 or bytes processed on success, -1 on error).
 */
uint64_t syscall_handler(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    if (sys_num >= MAX_SYSCALLS || syscall_table[sys_num] == NULL)
    {
        kprint("Kernel: unknown sys_num: ");
        kprint_hex_64(sys_num);
        kprint("\n");
        return -1;
    }

    return syscall_table[sys_num](arg1, arg2, arg3, arg4, arg5);
}

int8_t find_free_fd(Task *task)
{
    for (uint8_t i = 3; i < MAX_OPEN_FILES; i++)
    {
        if (task->fd_tbl[i] == NULL)
        {
            return i;
        }
    }

    return -1;
}