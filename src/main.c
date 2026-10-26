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
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
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
extern void sched_acquire_lock(uint64_t *rflags);
extern void sched_release_lock(uint64_t *rflags);
extern Task *sched_find_task_locked(int pid);

static void spawn_elf_app(const char *elf_path)
{
    kprint("Spawning App: ");
    kprint(elf_path);
    kprint("\n");

    Task *app_tsk = sched_new_task();
    if (app_tsk == NULL)
    {
        kprint("spawn_elf_app: OOM creating task\n");
        return;
    }
    strcpy(app_tsk->cwd, "/");

    uint64_t *app_pml4_virt = vmm_phys_to_hhdm(app_tsk->pml4);
    uint64_t entry = elf_load_PML4(elf_path, app_pml4_virt);

    if (entry != 0)
    {
        uint64_t virt_usr_stk_base = USER_STACK_TOP - (PAGE_SIZE * USER_STACK_PAGES);
        for (int i = 0; i < USER_STACK_PAGES; i++)
        {
            uint64_t phys_usr_stk = pmm_alloc_frame();
            if (phys_usr_stk == 0)
            {
                kprint("spawn_elf_app: OOM allocating user stack\n");
                sched_destroy_task(app_tsk);
                return;
            }
            memset(vmm_phys_to_hhdm(phys_usr_stk), 0, PAGE_SIZE);
            vmm_map_page(
                app_pml4_virt,
                virt_usr_stk_base + (i * PAGE_SIZE),
                phys_usr_stk,
                VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
        }

        size_t len = strlen(elf_path) + 1;
        uint64_t str_usr_addr = (USER_STACK_TOP - len) & ~0xFULL;
        uint64_t list_usr_addr = str_usr_addr - (2 * sizeof(uint64_t));
        if ((list_usr_addr & 0xFULL) == 0)
        {
            str_usr_addr -= 8;
            list_usr_addr = str_usr_addr - (2 * sizeof(uint64_t));
        }
        uint64_t rsp_usr_addr = list_usr_addr - sizeof(uint64_t);

        uint64_t top_page_phys = vmm_virt2phys(app_pml4_virt, str_usr_addr) & ~0xFFFULL;
        if (top_page_phys == 0)
        {
            kprint("spawn_elf_app: top_page_phys is NULL!\n");
            sched_destroy_task(app_tsk);
            return;
        }

        memcpy(vmm_phys_to_hhdm(top_page_phys + (str_usr_addr & 0xFFF)), elf_path, len);

        uint64_t *argv_kern = (uint64_t *)vmm_phys_to_hhdm(top_page_phys + (list_usr_addr & 0xFFF));
        argv_kern[0] = str_usr_addr;
        argv_kern[1] = 0;

        uint64_t *argc_kern = (uint64_t *)vmm_phys_to_hhdm(top_page_phys + (rsp_usr_addr & 0xFFF));
        *argc_kern = 1;

        task_context_setup(app_tsk, entry, rsp_usr_addr);
        if (app_tsk->kern_stk_top == 0)
        {
            kprint("spawn_elf_app: OOM allocating kernel stack\n");
            sched_destroy_task(app_tsk);
            return;
        }
        sched_register_task(app_tsk);
    }
    else
    {
        kprint("Failed to load app: ");
        kprint(elf_path);
        kprint("\n");
        sched_destroy_task(app_tsk);
    }
}

static inline void spawn_terminal(void)
{
    spawn_elf_app("/bin/terminal.elf");
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
    vfs_node_t *created_0 = fat32_create(fat_root, "test2.txt", VFS_FILE);
    if (created_0 != NULL)
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
        char *test_str = "Lorem Ipsum is simply dummy text of the printing and typesetting industry. Lorem Ipsum has been the industry's standard dummy text ever since the 1500s, when an unknown printer took a galley of type and scrambled it to make a type specimen book. It has survived not only five centuries, but also the leap into electronic typesetting, remaining essentially unchanged. It was popularised in the 1960s with the release of Letraset sheets containing Lorem Ipsum passages, and more recently with desktop publishing software like Aldus PageMaker including versions of Lorem Ipsum.";
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
    vfs_node_t *created = fat32_create(fat_root, "mydir", VFS_DIRECTORY);
    if (created != NULL)
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
    if (tick - prev_tick >= 50)
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
                win_draw_string(g_desktop_win, clock_x, clock_y, time_str, Yellow, 0xFF1A2228);
                video_add_dirty_rect(clock_x, clock_y, 8 * 8, 8);
            }
            kfree(t);
        }
    }
}

static inline void spawn_digital_clock(void)
{
    spawn_elf_app("/bin/clock_digital.elf");
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

    uint64_t tss_kern_stk = (uint64_t)vmm_phys_to_hhdm(pmm_alloc_frame()) + PAGE_SIZE;
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

    int clock_x = (int)video_get_width() - 72;
    int clock_y = (int)video_get_height() - TASKBAR_HEIGHT + 10;

    while (true)
    {
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
                    keyboard_get_char();
                    spawn_terminal();
                }
                else if (e.key == '\t' && is_alt)
                {
                    keyboard_get_char();
                    win_cycle_focus();
                }
                else
                {
                    Window *top_win = win_get_active();
                    if (top_win != NULL && top_win->owner_pid != -1)
                    {
                        uint64_t rflags;
                        sched_acquire_lock(&rflags);
                        Task *tsk = sched_find_task_locked(top_win->owner_pid);

                        if (tsk != NULL && tsk->state != TASK_DEAD && tsk->state != TASK_ZOMBIE && tsk->event_queue != NULL)
                        {
                            event_queue_push(tsk->event_queue, e);
                            if (tsk->state == TASK_SLEEPING || tsk->state == TASK_WAITING)
                            {
                                tsk->state = TASK_READY;
                            }
                        }

                        sched_release_lock(&rflags);
                    }
                }
                break;
            }
            case EVENT_WIN_RESIZE:
            {
                int pid = e.resize_event.win_owner_pid;
                uint64_t rflags;
                sched_acquire_lock(&rflags);
                Task *tsk = sched_find_task_locked(pid);
                if (tsk != NULL && tsk->state != TASK_DEAD && tsk->state != TASK_ZOMBIE && tsk->event_queue != NULL)
                {
                    event_queue_push(tsk->event_queue, e);
                    if (tsk->state == TASK_SLEEPING || tsk->state == TASK_WAITING)
                    {
                        tsk->state = TASK_READY;
                    }
                }
                sched_release_lock(&rflags);
                break;
            }
            default:
                break;
            }
        }

        sched_reap_zombies();
        cursor_erase();
        win_update();

        int launch_req = win_consume_launch_req();
        if (launch_req == LAUNCH_TERMINAL)
        {
            spawn_elf_app("/bin/terminal.elf");
        }
        else if (launch_req == LAUNCH_SNAKE)
        {
            spawn_elf_app("/bin/snake.elf");
        }
        else if (launch_req == LAUNCH_CLOCK)
        {
            spawn_elf_app("/bin/clock_analog.elf");
        }

        update_clock(clock_x, clock_y);

        win_paint();
        cursor_paint();
        video_swap();

        hlt();
    }
}
