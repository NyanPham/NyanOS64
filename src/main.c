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
#include "elf.h"

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

void *memcpy(void *restrict dest, const void *restrict src, size_t n) 
{
    uint8_t *restrict pdest = (uint8_t *restrict) dest;
    const uint8_t *restrict psrc = (const uint8_t *restrict) src;
    
    for (size_t i = 0; i < n; i++) 
    {
        pdest[i] = psrc[i];
    }

    return dest;
}
 
void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *) s;

    for (size_t i = 0; i < n; i++)
    {
        p[i] = (uint8_t) c;
    }

    return s;
}

void *memmove(void *dest, const void *src, size_t n)
{
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;
    
    if (src > dest)
    {
        for (size_t i = 0; i < n; i++)
        {
            pdest[i] = psrc[i];
        }
    }
    else if (src < dest)
    {
        for (size_t i = n; i > 0; i--) 
        {
            pdest[i-1] = psrc[i-1];
        }
    }
    
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;

    for (size_t i = 0; i < n; i++)
    {
        if (p1[i] != p2[i])
        {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }
    
    return 0;
}

static void hcf(void)
{
    for (;;)
    {
        asm ("hlt");
    }
}

extern uint64_t hhdm_offset;
extern uint64_t* kernel_pml4;
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

    serial_init();
    apic_init();
    keyboard_init();
    timer_init();

    syscall_init();

    /*=========== Kmalloc test ===========*/
    {
        void* test_ptr1 = kmalloc(32);
        if (test_ptr1 != NULL)
        {
            volatile uint32_t* kmalloc_test = (uint32_t*)test_ptr1;
            *kmalloc_test = 0xCAFEBABE;

            if (*kmalloc_test != 0xCAFEBABE) {
                hcf();
            }
            
            kfree(test_ptr1);
            
            void* test_ptr2 = kmalloc(32);
            if (test_ptr1 != test_ptr2) {
                hcf();
            }
            
            kfree(test_ptr2);
        }
        else 
        {
            hcf();
        }
    }

    /*=========== Test the initramfs ===========*/
    if (module_request.response == NULL || module_request.response->module_count < 1)
    {
        kprint("Error: no module found (initramfs)\n");
        hcf();
    } 

    struct limine_file* elf_file = module_request.response->modules[0];
    Elf64_Ehdr* elf_hdr = (Elf64_Ehdr*)elf_file->address;

    if (elf_hdr->e_ident[0] != ELF_MAGIC0 || 
        elf_hdr->e_ident[1] != ELF_MAGIC1 ||
        elf_hdr->e_ident[2] != ELF_MAGIC2 ||
        elf_hdr->e_ident[3] != ELF_MAGIC3
    )
    {
        kprint("Error: Not a valid ELF file\n");
        hcf();
    }

    Elf64_Phdr* phdr = (Elf64_Phdr*)((uint8_t*)elf_file->address + elf_hdr->e_phoff);
    for (size_t i = 0; i < elf_hdr->e_phnum; i++)
    {
        if (phdr[i].p_type == PT_LOAD)
        {
            kprint("Found PT_LOAD segment\n");
            kprint("    Offset in file: "); kprint_hex_64(phdr[i].p_offset); kprint("\n");
            kprint("    Virt Addr: "); kprint_hex_64(phdr[i].p_vaddr); kprint("\n");
            kprint("    File size: "); kprint_hex_64(phdr[i].p_filesz); kprint("\n");
            kprint("    Mem size: "); kprint_hex_64(phdr[i].p_memsz); kprint("\n");
            
            uint64_t npages = (phdr[i].p_memsz + PAGE_SIZE - 1) / PAGE_SIZE;
            for (size_t j = 0; j < npages; j++)
            {
                void* loc_virt_addr = pmm_alloc_frame();
                if (loc_virt_addr == NULL)
                {
                    kprint("Not enough memory!\n");
                    hcf();
                }

                uint64_t phys_addr = (uint64_t)loc_virt_addr - hhdm_offset;
                uint64_t targt_addr = phdr[i].p_vaddr + (j * PAGE_SIZE);

                vmm_map_page(kernel_pml4, targt_addr, phys_addr, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
                uint64_t off = j * PAGE_SIZE;
                uint64_t bytes = 0;

                if (off < phdr[i].p_filesz)
                {
                    uint64_t remaining_bytes = phdr[i].p_filesz - off;
                    bytes = remaining_bytes > PAGE_SIZE ? PAGE_SIZE : remaining_bytes;
                }

                if (bytes > 0)
                {
                    void* src = (uint8_t*)elf_file->address + phdr[i].p_offset + off;
                    memcpy(loc_virt_addr, src, bytes);
                }

                if (bytes < PAGE_SIZE)
                {
                    memset((uint8_t*)loc_virt_addr + bytes, 0, PAGE_SIZE - bytes);
                }
            }
            
            kprint("Loaded segment at ");
            kprint_hex_64(phdr[i].p_vaddr);
            kprint("\n");
        }
    }

    // test kprint  
    // if we reach here, at least the inits above,
    // if not working, don't crash our OS :)))
    kprint("Hello from the kernel side!\n");

    // check if we have the framebuffer to render on screen
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) 
    {
        hcf();
    }

    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];
    
    for (size_t i = 0; i < 100; i++)
    {
        volatile uint32_t *fb_ptr = framebuffer->address;
        fb_ptr[i * (framebuffer->pitch  / 4) + i] = 0xffffff;
    }

    kprint("Entering user mode...\n");
    uint64_t phys_usr_stk = pmm_alloc_frame() - hhdm_offset;
    vmm_map_page(kernel_pml4, USER_STACK_VIRT_ADDR, phys_usr_stk, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
    
    asm volatile(
        "mov %%rsp, %0" 
        : "=m"(kern_stk_ptr)
    );
    
    enter_user_mode(elf_hdr->e_entry, USER_STACK_VIRT_ADDR + PAGE_SIZE);

    asm volatile ("sti");
    hcf();
}
