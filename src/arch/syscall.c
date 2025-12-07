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
#include <stddef.h>
#include <stdbool.h>

#define IA32_EFER 0xC0000080    // a register that allows enabling the SYSCALL/SYSRET instruction (Extended Feature Enable Register)
#define IA32_STAR 0xC0000081    // a register that stores segment selectors for fast system calls, mostly for SYSCALL
#define IA32_LSTAR 0xC0000082   // a register to hold the 64-bit virt_addr of the syscall handler
#define IA32_FMASK 0xC0000084   // a register that masks (clears) specific flags in RFLAGS during a syscall
#define EFER_SCE 1              
#define INT_FLAGS 0x200 
#define REBOOT_PORT 0x64
#define KERN_BASE 0xFFFFFFFF80000000

extern void syscall_entry(void);

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

uint64_t syscall_handler(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    (void)arg3; (void)arg4; (void)arg5;
    switch (sys_num)
    {
        case 0:
        {
            // kprint("Kernel: Syscall 0 called (test)\n");
            return 0;
        }
        case 1:
        {
            // Sys_write(str, color)
            // kprint((const char*)arg1);
            video_write((const char*)arg1, (uint32_t)arg2);
            return 0;
        }
        case 2:
        {
            // Sys_read
            asm volatile("sti");
            while (1)
            {
                char c = keyboard_get_char();
                if (c != 0)
                {
                    return (uint64_t)c;
                }
                sched_block();
            }
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
            tar_list();
            return 0;
        }
        case 6:
        {
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
            // sys_exec
            uint64_t entry = elf_load((const char*)(arg1));
            if (entry == 0)
            {
                return -1;
            }

            sched_create_task(entry);
            return 0;
        }
        case 8:
        {
            // sys_exit
            sched_exit();
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