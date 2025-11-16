#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include "arch/gdt.h"
#include "arch/idt.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "mem/kmalloc.h"
#include "drivers/serial.h"
#include "drivers/apic.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "drivers/legacy/pit.h"
#include "drivers/legacy/pic.h"

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(4);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = 
{
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;


__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

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

void kmain(void)
{
    gdt_init();
    idt_init();

    if (LIMINE_BASE_REVISION_SUPPORTED == false)
    {
        hcf();
    }

    // check for the memory map response and the HHDM response.
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

    // test kmalloc
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

    // test kprint
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

    asm volatile ("sti");
    hcf();
}
