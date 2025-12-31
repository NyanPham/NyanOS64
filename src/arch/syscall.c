#include "syscall.h"
#include "cpu.h"
#include "gdt.h"
#include "drivers/video.h"
#include "drivers/keyboard.h"
#include "drivers/serial.h" // debugging
#include "../io.h"
#include "fs/tar.h"
#include "../string.h"
#include "elf.h"
#include "sched/sched.h"
#include "mem/kmalloc.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "gui/window.h"
#include "kern_defs.h"
#include "include/syscall_args.h"

#include <stddef.h>
#include <stdbool.h>

#define IA32_EFER 0xC0000080    // a register that allows enabling the SYSCALL/SYSRET instruction (Extended Feature Enable Register)
#define IA32_STAR 0xC0000081    // a register that stores segment selectors for fast system calls, mostly for SYSCALL
#define IA32_LSTAR 0xC0000082   // a register to hold the 64-bit virt_addr of the syscall handler
#define IA32_FMASK 0xC0000084   // a register that masks (clears) specific flags in RFLAGS during a syscall
#define EFER_SCE 1              
#define INT_FLAGS 0x200 
#define REBOOT_PORT 0x64

extern uint64_t hhdm_offset;
extern void syscall_entry(void);
int8_t find_free_fd(Task* task);

static bool verify_usr_access(uint64_t ptr, uint64_t size)
{
    uint64_t res = (ptr + size);
    return (res >= ptr && res >= size && res <= KERN_BASE);
}

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
 * | 6         | sys_read_file    | Reads a file content (Legacy/Deprecated).  |
 * | 7         | sys_exec         | Executes a new program (ELF).              |
 * | 8         | sys_exit         | Terminates the current process.            |
 * | 9         | sys_waitpid      | Waits for a child process to exit.         |
 * | 10        | sys_open         | Opens a file.                              |
 * | 11        | sys_close        | Closes a file descriptor.                  |
 * | 12        | sys_sbrk         | Change program break / allocate user heap  |
 * | 13        | sys_kprint       | Kernel debug print callable from userland |
 * | 14        | sys_get_key      | Reads a key from the keyboard buffer      |
 * | 15        | sys_chdir        | Change directory                          |
 * | 16        | sys_getcwd       | Get current working directory             |
 *
 * @param sys_num The system call number.
 * @return The return value of the system call (typically 0 or bytes processed on success, -1 on error).
 */
uint64_t syscall_handler(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    (void)arg4; (void)arg5;
    switch (sys_num)
    {
        case 0:
        {
            /*
            sys_read(fd, buf, count)
            */
            int8_t fd = (int8_t)arg1;
            char* buf = (char*)arg2;
            uint64_t count = arg3;

            // validate fd
            if (fd < 0 || fd >= MAX_OPEN_FILES)
            {
                return -1;
            }

            Task* curr_tsk = get_curr_task(); 
            if (curr_tsk->fd_tbl[fd] == NULL)
            {
                return -1;
            }

            // validate user access
            if (!verify_usr_access((uint64_t)buf, count))
            {
                kprint("SYS_READ: invalid buffer\n");
                return -1;
            }

            // call vfs (if fd == 0, we read the stdin_read)
            return vfs_read(curr_tsk->fd_tbl[fd], count, (uint8_t*)buf);
        }
        case 1:
        {
            // sys_write(fd, buf, count)

            int8_t fd = (int8_t)arg1;
            char* buf = (char*)arg2;
            uint64_t count = arg3;

            // validate fd
            if (fd < 0 || fd >= MAX_OPEN_FILES)
            {
                return -1;
            }

            Task* curr_tsk = get_curr_task();
            if (curr_tsk->fd_tbl[fd] == NULL)
            {
                return -1;
            }

            // validate user access
            if (!verify_usr_access((uint64_t)buf, count))
            {
                kprint("SYS_WRITE: invalid buffer\n");
                return -1;
            }

            if (curr_tsk->win != NULL && (fd == 1 || fd == 2))
            {
                // detour the flow to print within the 
                // window of task rather than the universal screen

                for (uint64_t i = 0; i < count; i++)
                {
                    win_put_char(curr_tsk->win, buf[i]);
                }

                return count; 
            }
            else 
            {
                // call vfs (if fd == 1, it calls the stdout_write)
                return vfs_write(curr_tsk->fd_tbl[fd], count, (uint8_t*)buf);
            }
        }
        case 2:
        {
            kprint("SYSCALL 2: RESERVED");
            return 0;
        }
        case 3:
        {
            // Sys_clear
            video_clear();
            return 0;
        }
        case 4:
        {
            outb(REBOOT_PORT, 0xFE);
            return 0;
        }
        case 5:
        {
            //  Sys_list_files
            tar_list((char* )arg1, arg2);
            return 0;
        }
        case 6:
        {
            /* DEPRECATED */
            // We have case 12 to read a file with VFS instead, 
            // this is kept for legacy for now, and reserved
            // for future use 

            // Sys_read_file(fname, buf)
            char* content = tar_read_file((const char*)arg1);
            if (content == NULL)
            {
                kprint("DEBUG_CAT: tar_read_file returned NULL!\n"); // Debug fail
                return -1;
            }
            char* dest = (char*)arg2;

            if (!verify_usr_access(dest, strlen(content) + 1))
            {
                kprint("Invalid user access memory\n");
                return -1;   
            }

            strcpy(dest, content);
            return 0;
        }
        case 7:
        {
            // sys_exec(char* path, char** argv)
            char* raw_path = (char*)arg1;
            char** argv = (char**)arg2;
            
            int argc = 0;
            size_t argv_size = 0;
            while (argv[argc] != NULL)
            {
                argv_size += strlen(argv[argc]) + 1;
                argc++;
            }

            size_t argv_ptr_size = (argc + 1) * sizeof(uint64_t) ;
            
            char* argv_buf = kmalloc(argv_size);

            int cpy_off = 0;
            for (int i = 0; i < argc; i++)
            {
                size_t len = strlen(argv[i]) + 1;
                memcpy(&argv_buf[cpy_off], argv[i], len);
                cpy_off += len;
            }

            uint64_t start_usr_addr = USER_STACK_TOP - argv_size; // address to store the argv content in the new stack
            uint64_t* argv_list = (uint64_t*)kmalloc(argv_ptr_size);

            size_t curr_off = 0;
            for (int i = 0; i < argc; i++)
            {
                argv_list[i] = start_usr_addr + curr_off;
                curr_off += strlen(argv[i]) + 1; 
            }

            argv_list[argc] = 0;

            // handle the path
            Task* curr_tsk = get_curr_task();
            char full_path[256];
            resolve_path(curr_tsk->cwd, raw_path, full_path);

            char* elf_fname = kmalloc(strlen(full_path) + 1);
            strcpy(elf_fname, full_path);

            uint64_t old_pml4 = read_cr3();

            Task* task = sched_new_task();
            task->heap_end = USER_HEAP_START;
            write_cr3(task->pml4);

            uint64_t virt_usr_stk_base = USER_STACK_TOP - PAGE_SIZE;
            uint64_t phys_usr_stk = (uint64_t)pmm_alloc_frame() - hhdm_offset;

            vmm_map_page(
                (uint64_t*)(task->pml4 + hhdm_offset),
                virt_usr_stk_base,
                phys_usr_stk,
                VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER
            );

            // copy the argv_buf
            uint64_t offset_buf = start_usr_addr & 0xFFF;
            void* dest_buf = (void*)(phys_usr_stk + offset_buf + hhdm_offset);
            memcpy(dest_buf, argv_buf, argv_size);

            // copy the argv_list
            uint64_t start_list_addr = start_usr_addr - argv_ptr_size; 
            uint64_t offset_list = start_list_addr & 0xFFF;
            void* dest_list = (void*)(phys_usr_stk + offset_list + hhdm_offset);
            memcpy(dest_list, argv_list, argv_ptr_size);

            // write argc into the rsp top
            // virt_rsp = start_list_addr - 8
            uint64_t rsp_virt_addr = start_list_addr - sizeof(uint64_t);
            uint64_t offset_rsp = rsp_virt_addr & 0xFFF;
            uint64_t* dest_rsp = (uint64_t*)(phys_usr_stk + offset_rsp + hhdm_offset);
            *dest_rsp = argc;

            kfree(argv_buf);
            kfree(argv_list);

            uint64_t entry = elf_load(elf_fname);

            if (entry == 0)
            {
                write_cr3(old_pml4);
                kfree(elf_fname);
                sched_destroy_task(task);
                return -1;
            }

            write_cr3(old_pml4);
            kfree(elf_fname);

            sched_load_task(task, entry, rsp_virt_addr);

            return task->pid;
        }
        case 8:
        {
            // sys_exit
            sched_exit((int)(arg1));
            return 0;
        }
        case 9:
        {
            // sys_waitpid(pid, stat)
            int pid = (int)arg1;
            int* stat = (int*)arg2;

            Task* child = sched_find_task(pid);
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
                        if (!verify_usr_access(stat, sizeof(int)))
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
        case 10:
        {
            // sys_open
            char* fname = (char*)arg1;
            uint32_t mode = (uint32_t)arg2;

            Task* curr_tsk = get_curr_task();

            int8_t fd = find_free_fd(curr_tsk);
            if (fd < 0)
            {
                return -1;
            }

            file_handle_t* f = vfs_open(fname, mode);
            if (f == NULL)
            {
                return -1;
            }

            curr_tsk->fd_tbl[fd] = f;
            return fd;
        }
        case 11:
        {
            // sys_close
            int8_t fd = (int8_t)arg1;
            if (fd < 0 || fd >= MAX_OPEN_FILES)
            {
                return -1;
            }

            Task* curr_tsk = get_curr_task();
            if (curr_tsk->fd_tbl[fd] == NULL)
            {
                return -1;
            }

            vfs_close(curr_tsk->fd_tbl[fd]);
            curr_tsk->fd_tbl[fd] = NULL;
            return 0;
        }
        case 12:
        {
            // sys_sbrk(int64_t incr_payload)
            int64_t incr_payload = (int64_t)arg1;
            Task* curr_tsk = get_curr_task();

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
                void *phys_addr_hhdm = pmm_alloc_frame();
                if (phys_addr_hhdm == NULL)
                {
                    kprint("SYS_BRK: out of memory!\n");
                    return -1;
                }
                
                memset(phys_addr_hhdm, 0, PAGE_SIZE); // Security: Clean the page

                uint64_t phys_addr = (uint64_t)phys_addr_hhdm - hhdm_offset;
                
                vmm_map_page(
                    (uint64_t*)(curr_tsk->pml4 + hhdm_offset),
                    virt_addr,
                    (uint64_t)phys_addr,
                    VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER
                );
            }
            curr_tsk->heap_end = next_brk;
            return prev_brk;
        }
        case 13:
        {
            // sys_kprint(char* s)
            char* s = (char*)arg1;
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
        case 14:
        {
            // sys_get_key()
            return (uint64_t)keyboard_get_char();
        }
        case 15:
        {
            // sys_chdir(path)
            char* path = (char*) arg1;
            if (!verify_usr_access((uint64_t)path, 1))
            {
                return -1;
            }

            Task* curr_tsk = get_curr_task();

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
        case 16:
        {
            // sys_getcwd(buf, size)
            char* buf = (char*)arg1;
            uint64_t size = arg2;

            if (!verify_usr_access((uint64_t)buf, size))
            {
                return -1;
            }

            Task* curr_tsk = get_curr_task();
            uint64_t len = strlen(curr_tsk->cwd);

            if (size < len + 1)
            {
                return -1;
            }

            strcpy(buf, curr_tsk->cwd);
            return 0;
        }
        case 17:
        {
            // sys_create_win(win_params)

            WinParams_t* win_params = (WinParams_t*)arg1;
            
            if (win_params == NULL)
            {
                kprint("Not having window params passed!\n");
                return -1;
            }

            if (!verify_usr_access(win_params, sizeof(WinParams_t)))
            {
                kprint("Invalid WinParams space!\n");
                return -1;
            }

            char w_title[256]; 
            strncpy(w_title, win_params->title, 256);
            w_title[255] = 0;

            Task* tsk = get_curr_task();
            if (tsk->win != NULL)
            {
                kprint("Task already has window, on attempt to create win: ");
                kprint(w_title);
                kprint("\n");

                return -1;
            }
            
            create_win(win_params->x, win_params->y, win_params->width, win_params->height, w_title);
            return 0;
        }
        case 18:
        {
            // kprint_int(int x )
            int x = (int)arg1;
            kprint_int(x);
            return 0;
        }
        default:
        {
            kprint("Kernel: unknown sys_num: ");
            kprint_hex_64(sys_num);
            kprint("\n");
            return 0;
        }
    }
}

int8_t find_free_fd(Task* task)
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

void resolve_path(const char* cwd, const char* inp_path, char* out_buf)
{
    char tmp[256];

    // First, let's find the starting point
    if (inp_path[0] == '/')
    {
        // absolute path
        strcpy(tmp, inp_path);
    }
    else
    {
        // relative path -> cwd + "/" + inp_path
        strcpy(tmp, cwd);
        int len = strlen(tmp);
        if (len > 1 && tmp[len-1] != '/')
        {
            strcat(tmp, "/");
        }
        strcat(tmp, inp_path);
    }

    // Second, handle the Stack logic with `..` and `.`
    char stack[32][32];
    int top = 0;
    int i = 0;

    if (tmp[0] == '/')
    {
        i = 1;
    }

    char name_buf[32];
    int n_idx = 0;

    while (1)
    {
        char c = tmp[i];

        if (c == '/' || c == 0)
        {
            name_buf[n_idx] = 0;
            if (n_idx > 0)
            {
                if (strcmp(name_buf, "..") == 0)
                {
                    if (top > 0) top--;
                }
                else if (strcmp(name_buf, ".") == 0)
                {
                    // ignore
                }
                else
                {
                    strcpy(stack[top++], name_buf);
                }
            }
            n_idx = 0;
            if (c == 0)
            {
                break;
            }
        }
        else 
        {
            name_buf[n_idx++] = c;
        }
        i++;
    }

    // Last, rebuild the stack path
    strcpy(out_buf, "/");
    for (int k = 0; k < top; k++)
    {
        if (k > 0)
        {
            strcat(out_buf, "/");
        }
        strcat(out_buf, stack[k]);
    }
}