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
#include "drivers/rtc.h"
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

extern uint64_t *kern_pml4;
extern uint64_t kern_stk_ptr;
extern void enter_user_mode(uint64_t entry, uint64_t usr_stk_ptr);
extern EventBuf g_event_queue;
extern Window *g_desktop_win;

static inline void spawn_terminal()
{
    kprint("Loading Terminal App...\n");

    Task *term_tsk = sched_new_task();
    if (term_tsk == NULL)
    {
        return;
    }
    strcpy(term_tsk->cwd, "/");

    uint64_t curr_pml4 = read_cr3(); // this could be kern_pml4, but nah, let's make thing variable :))

    write_cr3(term_tsk->pml4);

    uint64_t term_entry = elf_load("/bin/terminal.elf");

    if (term_entry != 0)
    {
        kprint("Loading Terminal...\n");

        uint64_t virt_usr_stk_base = USER_STACK_TOP - PAGE_SIZE;
        uint64_t phys_usr_stk = vmm_hhdm_to_phys(pmm_alloc_frame());

        vmm_map_page(
            vmm_phys_to_hhdm(term_tsk->pml4),
            virt_usr_stk_base,
            phys_usr_stk,
            VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
        // no args, so argc = 0, no need space for argv
        uint64_t *kern_view_stk = vmm_phys_to_hhdm(phys_usr_stk + PAGE_SIZE - sizeof(uint64_t));
        *kern_view_stk = 0;

        uint64_t term_rsp = USER_STACK_TOP - sizeof(uint64_t);

        write_cr3(curr_pml4);

        task_context_setup(term_tsk, term_entry, term_rsp);
        sched_register_task(term_tsk);
    }
    else
    {
        write_cr3(curr_pml4);
        kprint("Failed to load terminal!\n");
        sched_destroy_task(term_tsk);
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

void test_tar_fs()
{
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

void test_fat32(vfs_node_t *fat_root)
{
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

    kprint("Creating test2.txt...\n");
    int res = fat32_create(fat_root, "test2.txt", VFS_FILE);
    if (res == 0)
    {
        kprint("Creation successful! Listing root:\n");
        k_ls("/data");
    }
    else
    {
        kprint("Creation failed.\n");
    }

    kprint("Writing content to test2.txt...\n");
    file_handle_t *f_write = vfs_open("/data/test2.txt", 0);
    if (f_write)
    {
        char *test_str = "Hello NyanOS! FAT32 Write works perfectly!";
        uint64_t len = strlen(test_str);
        uint64_t written = vfs_write(f_write, len, (uint8_t *)test_str);
        kprint("Written: ");
        kprint_int(written);
        kprint(" bytes.\n");

        vfs_close(f_write);
    }
    else
    {
        kprint("Failed to open test2.txt for writing!\n");
    }

    kprint("Verifying content of test2.txt...\n");
    file_handle_t *f_read = vfs_open("/data/test2.txt", 0);
    if (f_read)
    {
        char buf[100];
        uint64_t bytes_read = vfs_read(f_read, 100, (uint8_t *)buf);
        buf[bytes_read] = 0;

        kprint("Read back: [");
        kprint(buf);
        kprint("]\n");

        kprint("File Size in VFS: ");
        kprint_int(f_read->node->length);
        kprint("\n");

        vfs_close(f_read);
    }
    else
    {
        kprint("Failed to open test2.txt for verification!\n");
    }

    kprint("\nCreating directory 'MYDIR'...\n");
    int res_dir = fat32_create(fat_root, "mydir", VFS_DIRECTORY);
    if (res_dir == 0)
    {
        kprint("Folder creation successful!\n");
        k_ls("/data/mydir");
    }
    else
    {
        kprint("Folder creation failed\n");
    }
}

static uint64_t prev_tick = 0;
void update_clock(int clock_x, int clock_y)
{
    uint64_t tick = timer_get_ticks();
    if (tick - prev_tick >= 100)
    {
        prev_tick = tick;
        Time_t *t = rtc_get_time();
        if (t)
        {
            uint8_t vn_hrs = (t->hrs + 7) % 24;
            char time_str[9];

            time_str[0] = (vn_hrs / 10) + '0';
            time_str[1] = (vn_hrs % 10) + '0';
            time_str[2] = ':';
            time_str[3] = (t->mins / 10) + '0';
            time_str[4] = (t->mins % 10) + '0';
            time_str[5] = ':';
            time_str[6] = (t->secs / 10) + '0';
            time_str[7] = (t->secs % 10) + '0';
            time_str[8] = '\0';

            if (g_desktop_win)
            {
                win_draw_string(g_desktop_win, clock_x, clock_y, "        ", White, Teal);
                win_draw_string(g_desktop_win, clock_x, clock_y, time_str, White, Teal);
                video_add_dirty_rect(clock_x, clock_y, 8 * 8, 8);
            }
            kfree(t);
        }
    }
}

static inline void spawn_digital_clock()
{
    kprint("Spawning Digital Clock App...\n");

    Task *clock_task = sched_new_task();
    memset(clock_task->fpu_regs, 0, 528);
    uint64_t curr_pml4 = read_cr3();
    write_cr3(clock_task->pml4);

    uint64_t entry = elf_load("/bin/clock_digital.elf");

    if (entry != 0)
    {
        uint64_t virt_usr_stk_base = USER_STACK_TOP - PAGE_SIZE;
        uint64_t phys_usr_stk = vmm_hhdm_to_phys(pmm_alloc_frame());

        vmm_map_page(
            vmm_phys_to_hhdm(clock_task->pml4),
            virt_usr_stk_base,
            phys_usr_stk,
            VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);

        uint64_t *kern_view_stk = vmm_phys_to_hhdm(phys_usr_stk + PAGE_SIZE - sizeof(uint64_t));
        *kern_view_stk = 0;

        uint64_t user_rsp = USER_STACK_TOP - sizeof(uint64_t);

        write_cr3(curr_pml4);

        task_context_setup(clock_task, entry, user_rsp);
        sched_register_task(clock_task);

        kprint("Clock App Spawned!\n");
    }
    else
    {
        write_cr3(curr_pml4);
        kprint("Failed to load /bin/clock.elf!\n");
        sched_destroy_task(clock_task);
    }
}

void kmain(void)
{
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false)
    {
        hcf();
    }

    gdt_init();
    idt_init();
    enable_sse();

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
    init_win_manager();

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

        test_tar_fs();
    }
    else
    {
        kprint("Warning: ROOTFS.TAR not found.\n");
    }

    vfs_node_t *fat_root = fat32_init_fs(0, 1);
    vfs_mount("/data", fat_root);

    test_fat32(fat_root);

    cursor_init();

    // test kprint
    // if we reach here, at least the inits above,
    // if not working, don't crash our OS :)))
    kprint("Hello from the kernel side!\n");

    spawn_digital_clock();

    // int clock_x = (int)video_get_width() - 80;
    // int clock_y = (int)video_get_height() - 80;

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
                    kprint("Hotkey detected to spawn a terminal\n");
                    keyboard_get_char(); // consume the pressed "t"
                    spawn_terminal();
                }
                else
                {
                    Window *top_win = win_get_active();
                    if (top_win != NULL && top_win->owner_pid != -1)
                    {
                        Task *tsk = sched_find_task(top_win->owner_pid);
                        if (tsk != NULL && tsk->event_queue != NULL)
                        {
                            event_queue_push(tsk->event_queue, e);
                            sched_wake_pid(top_win->owner_pid);
                        }
                    }
                }
                break;
            }
            case EVENT_WIN_RESIZE:
            {
                int pid = e.resize_event.win_owner_pid;
                Task *tsk = sched_find_task(pid);
                if (tsk != NULL && tsk->event_queue != NULL)
                {
                    event_queue_push(tsk->event_queue, e);
                    sched_wake_pid(pid);
                }
                break;
            }
            default:
            {
                // do nothing
            }
            }
        }

        cursor_erase();
        win_update();

        win_paint();
        cursor_paint();
        video_swap();

        hlt();
    }
}
