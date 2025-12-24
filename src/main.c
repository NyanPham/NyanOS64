#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include "arch/gdt.h"
#include "arch/idt.h"
#include "arch/syscall.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "mem/kmalloc.h"
#include "drivers/serial.h"
#include "drivers/apic.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "drivers/legacy/pit.h"
#include "drivers/legacy/pic.h"
#include "drivers/video.h"
#include "sched/sched.h"
#include "fs/dev.h"
#include "elf.h"
#include "fs/tar.h"
#include "fs/vfs.h"
#include "fs/tar_fs.h"
#include "cpu.h"
#include "string.h"
#include "gui/window.h"
#include "kern_defs.h"

#define USER_STACK_VIRT_ADDR 0x500000

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[3] = LIMINE_BASE_REVISION(4);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = 
{
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = 
{
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request =
{
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request =
{
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[4] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[2] = LIMINE_REQUESTS_END_MARKER;

static void hcf(void)
{
    for (;;)
    {
        asm ("hlt");
    }
}

extern uint64_t hhdm_offset;
extern uint64_t* kern_pml4;
extern uint64_t kern_stk_ptr;
extern void enter_user_mode(uint64_t entry, uint64_t usr_stk_ptr);

void kmain(void)
{
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false)
    {
        hcf();
    }

    gdt_init();
    idt_init();

    /*=========== Check for memmap response and HHDM response ===========*/
    if (memmap_request.response == NULL || hhdm_request.response == NULL)
    {
        hcf();
    }
    
    pmm_init(memmap_request.response, hhdm_request.response);
    vmm_init();

    uint64_t tss_kern_stk = (uint64_t)pmm_alloc_frame() + PAGE_SIZE;
    tss_set_stack(tss_kern_stk);

    serial_init();
    apic_init();
    keyboard_init();
    mouse_init();
    timer_init();
    init_window_manager();

    syscall_init();
    dev_init_stdio();
    sched_init();

    /*=========== Kmalloc test ===========*/
    // {
    //     void* test_ptr1 = kmalloc(32);
    //     if (test_ptr1 != NULL)
    //     {
    //         volatile uint32_t* kmalloc_test = (uint32_t*)test_ptr1;
    //         *kmalloc_test = 0xCAFEBABE;

    //         if (*kmalloc_test != 0xCAFEBABE) {
    //             hcf();
    //         }
            
    //         kfree(test_ptr1);
            
    //         void* test_ptr2 = kmalloc(32);
    //         if (test_ptr1 != test_ptr2) {
    //             hcf();
    //         }
            
    //         kfree(test_ptr2);
    //     }
    //     else 
    //     {
    //         hcf();
    //     }
    // }

    
    // check if we have the framebuffer to render on screen
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) 
    {
        hcf();
    }
    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    video_init(fb);

    // int rect_x = 0;
    // int rect_y = 200;


    video_write("Welcome to NyanOS kernel!\n", 0x00FF00);    

    /*=========== Test the initramfs ===========*/
    if (module_request.response == NULL || module_request.response->module_count < 1)
    {
        kprint("Error: no module found (initramfs)\n");
        hcf();
    } 

    if (module_request.response->module_count >= 2)
    {
        struct limine_file* tar_file = module_request.response->modules[1];
        kprint("Initializing VFS...\n");
        vfs_init();

        vfs_node_t* tar_root = tar_fs_init(tar_file->address);
        vfs_mount("/", tar_root);
        
        // test VFS
        kprint("VFS Test: Opening hello.txt...\n");
        file_handle_t* f = vfs_open("hello.txt", 0);
        if (f)
        {
            kprint("VFS TEST: Success! Found hello.txt\n");
            
            // read some bytes
            char buf[32];
            uint64_t bytes_read = vfs_read(f, 13, (uint8_t*)buf);
            buf[bytes_read] = 0;
            kprint("Content: "); 
            kprint(buf);
            kprint("\n");

            vfs_close(f);
        }
        else
        {
            kprint("VFS TEST: Failed to open hello.txt\n");
        }
    }
    else
    {
        kprint("Warning: ROOTFS.TAR not found.\n");
    }

    kprint("Loading Shell...\n");

    Task* shell_task = sched_new_task();

    uint64_t curr_pml4 = read_cr3(); // this could be kern_pml4, but nah, let's make thing variable :))

    write_cr3(shell_task->pml4);

    uint64_t shell_entry = elf_load("shell.elf");

    if (shell_entry != 0)
    {
        kprint("Loading User Task...\n");

        uint64_t virt_usr_stk_base = USER_STACK_TOP - PAGE_SIZE;
        uint64_t phys_usr_stk = (uint64_t)pmm_alloc_frame() - hhdm_offset;
        
        vmm_map_page(
            (uint64_t*)(shell_task->pml4 + hhdm_offset),
            virt_usr_stk_base,
            phys_usr_stk,
            VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER
        );
        // no args, so argc = 0, no need space for argv
        uint64_t* kern_view_stk = (uint64_t*)(phys_usr_stk + PAGE_SIZE - sizeof(uint64_t) + hhdm_offset);
        *kern_view_stk = 0;

        uint64_t shell_rsp = USER_STACK_TOP - sizeof(uint64_t);
        
        write_cr3(curr_pml4);
        sched_load_task(shell_task, shell_entry, shell_rsp);
    }
    else 
    {
        write_cr3(curr_pml4);
        kprint("Failed to load Shell!\n");
        sched_destroy_task(shell_task);
        hcf();
    }

    // test kprint  
    // if we reach here, at least the inits above,
    // if not working, don't crash our OS :)))
    kprint("Hello from the kernel side!\n");

    asm volatile ("sti");
    hcf();
}
