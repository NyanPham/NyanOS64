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
#include "drivers/ata.h"
#include "sched/sched.h"
#include "fs/dev.h"
#include "elf.h"
#include "fs/tar.h"
#include "fs/vfs.h"
#include "fs/tar_fs.h"
#include "fs/fat32.h"
#include "cpu.h"
#include "./string.h"
#include "gui/window.h"
#include "gui/cursor.h"
#include "gui/terminal.h"
#include "event/event.h"
#include "kern_defs.h"
#include "include/signal.h"
#include "utils/asm_instrs.h"

#define USER_STACK_VIRT_ADDR 0x500000

__attribute__((used, section(".limine_requests"))) static volatile uint64_t limine_base_revision[3] = LIMINE_BASE_REVISION(4);

__attribute__((used, section(".limine_requests"))) static volatile struct limine_framebuffer_request framebuffer_request =
    {
        .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
        .revision = 0};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_memmap_request memmap_request =
    {
        .id = LIMINE_MEMMAP_REQUEST_ID,
        .revision = 0};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_hhdm_request hhdm_request =
    {
        .id = LIMINE_HHDM_REQUEST_ID,
        .revision = 0};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_module_request module_request =
    {
        .id = LIMINE_MODULE_REQUEST_ID,
        .revision = 0};

__attribute__((used, section(".limine_requests_start"))) static volatile uint64_t limine_requests_start_marker[4] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end"))) static volatile uint64_t limine_requests_end_marker[2] = LIMINE_REQUESTS_END_MARKER;

extern uint64_t hhdm_offset;
extern uint64_t *kern_pml4;
extern uint64_t kern_stk_ptr;
extern void enter_user_mode(uint64_t entry, uint64_t usr_stk_ptr);
extern EventBuf g_event_queue;

static inline void spawn_shell()
{
    kprint("Loading Shell...\n");

    Task *shell_task = sched_new_task();
    Terminal *console = term_create(
        100, 100,
        370, 270,
        700, "Shell",
        WIN_MOVABLE | WIN_RESIZEABLE | WIN_MINIMIZABLE);
    shell_task->term = console;
    console->win->owner_pid = shell_task->pid;
    strcpy(shell_task->cwd, "/");

    uint64_t curr_pml4 = read_cr3(); // this could be kern_pml4, but nah, let's make thing variable :))

    write_cr3(shell_task->pml4);

    uint64_t shell_entry = elf_load("shell.elf");

    if (shell_entry != 0)
    {
        kprint("Loading User Task...\n");

        uint64_t virt_usr_stk_base = USER_STACK_TOP - PAGE_SIZE;
        uint64_t phys_usr_stk = (uint64_t)pmm_alloc_frame() - hhdm_offset;

        vmm_map_page(
            (uint64_t *)(shell_task->pml4 + hhdm_offset),
            virt_usr_stk_base,
            phys_usr_stk,
            VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
        // no args, so argc = 0, no need space for argv
        uint64_t *kern_view_stk = (uint64_t *)(phys_usr_stk + PAGE_SIZE - sizeof(uint64_t) + hhdm_offset);
        *kern_view_stk = 0;

        uint64_t shell_rsp = USER_STACK_TOP - sizeof(uint64_t);

        write_cr3(curr_pml4);

        task_context_setup(shell_task, shell_entry, shell_rsp);
        sched_register_task(shell_task);
    }
    else
    {
        write_cr3(curr_pml4);
        kprint("Failed to load Shell!\n");
        sched_destroy_task(shell_task);
    }
}

void k_ls(const char *path)
{
    kprint("\n--- LS Command Testing: ");
    kprint(path);
    kprint(" ---\n");

    file_handle_t *f = vfs_open(path, 0);
    if (f == NULL)
    {
        kprint("k_ls: Failed to open path!\n");
        return;
    }

    if ((f->node->flags & VFS_DIRECTORY) == 0)
    {
        kprint("k_ls: Not a directory!\n");
        vfs_close(f);
        return;
    }

    dirent_t entry;
    int index = 0;

    while (vfs_readdir(f->node, index, &entry) == 0)
    {
        if (entry.type == VFS_DIRECTORY)
        {
            kprint("[DIR ] ");
        }
        else
        {
            kprint("[FILE] ");
        }
        kprint(entry.name);
        kprint(" - ");
        kprint_int(entry.size);
        kprint(" bytes\n");
        index++;
    }
    kprint("--- End of List ---\n");
    vfs_close(f);
}

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
    init_win_manager();

    syscall_init();
    dev_init_stdio();
    sched_init();
    ata_identify(1);
    sti();
    ata_fs_init();

    // check if we have the framebuffer to render on screen
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1)
    {
        hcf();
    }
    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    video_init(fb);

    /*=========== Test the initramfs ===========*/
    if (module_request.response == NULL || module_request.response->module_count < 1)
    {
        kprint("Error: no module found (initramfs)\n");
        hcf();
    }

    if (module_request.response->module_count >= 2)
    {
        struct limine_file *tar_file = module_request.response->modules[1];
        kprint("Initializing VFS...\n");
        vfs_init();

        vfs_node_t *tar_root = tar_fs_init(tar_file->address);
        vfs_mount("/", tar_root);

        // test VFS
        kprint("VFS Test: Opening hello.txt...\n");
        file_handle_t *f = vfs_open("/hello.txt", 0);
        if (f)
        {
            kprint("VFS TEST: Success! Found /hello.txt\n");

            // read some bytes
            char buf[32];
            uint64_t bytes_read = vfs_read(f, 13, (uint8_t *)buf);
            buf[bytes_read] = 0;
            kprint("Content: ");
            kprint(buf);
            kprint("\n");

            vfs_close(f);
        }
        else
        {
            kprint("VFS TEST: Failed to open /hello.txt\n");
        }
    }
    else
    {
        kprint("Warning: ROOTFS.TAR not found.\n");
    }

    vfs_node_t *fat_root = fat32_init_fs(0, 1);
    vfs_mount("/data", fat_root);

    file_handle_t *f1 = vfs_open("/data/TEST.TXT", 0);
    if (f1)
    {
        kprint("VFS TEST: Success! Found /data/TEST.TXT\n");

        // read some bytes
        char buf[65];
        uint64_t bytes_read = vfs_read(f1, 64, (uint8_t *)buf);
        buf[bytes_read] = 0;
        kprint("Content: ");
        kprint(buf);
        kprint("\n");

        vfs_close(f1);
    }
    else
    {
        kprint("VFS TEST: Failed to open /data/TEST.TXT\n");
    }

    k_ls("/");
    k_ls("/data");

    // test kprint
    // if we reach here, at least the inits above,
    // if not working, don't crash our OS :)))
    kprint("Hello from the kernel side!\n");

    cursor_init();
    while (true)
    {
        // Event Loop
        Event e;
        if (event_queue_pop(&g_event_queue, &e))
        {
            switch (e.type)
            {
            case EVENT_KEY_PRESSED:
            {
                bool is_ctrl = e.modifiers & MOD_CTRL;
                bool is_alt = e.modifiers & MOD_ALT;

                if (e.key == 't' && is_ctrl && is_alt)
                {
                    kprint("Hotkey detected to spawn a shell\n");
                    keyboard_get_char(); // consume the pressed "t"
                    spawn_shell();
                }
                else if (e.key == 'c' && is_ctrl)
                {
                    kprint("Hotkey detected to kill a process\n");
                    keyboard_get_char(); // consume the pressed "c"
                    Window *top_win = win_get_active();
                    if (top_win != NULL && top_win->owner_pid != -1)
                    {
                        sched_send_signal(top_win->owner_pid, SIGINT);
                        sched_wake_pid(top_win->owner_pid);
                    }
                    else
                    {
                        kprint("No active process to kill.\n");
                    }
                }
                else
                {
                    Window *top_win = win_get_active();
                    if (top_win != NULL && top_win->owner_pid != -1)
                    {
                        Task *tsk = sched_find_task(top_win->owner_pid);
                        if (tsk && tsk->term)
                        {
                            term_process_input(tsk->term, e.key);
                        }
                        sched_wake_pid(top_win->owner_pid);
                    }
                }
                break;
            }
            case EVENT_WIN_RESIZE:
            {
                int pid = e.resize_event.win_owner_pid;
                Task *tsk = sched_find_task(pid);
                if (tsk != NULL && tsk->term != NULL)
                {
                    term_resize(tsk->term, tsk->term->win->width, tsk->term->win->height);
                }
            }
            default:
            {
                // do nothing
            }
            }
        }

        win_update();

        video_clear();
        video_draw_string(10, 10, "Welcome to NyanOS kernel!", White);
        video_draw_string(10, 20, "Press `Ctrl + Alt + T` to run a Terminal!", White);

        term_paint();
        term_blink_active();
        win_paint();
        draw_mouse();
        video_swap();

        hlt();
    }
}
