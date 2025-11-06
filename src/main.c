#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "pit.h"
#include "page_frame_allocator.h"

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

// Test for the page frame allocator.
static void test_pfa(void) {
    // test the cache refill mechanism.
    const int num_test_frames = 30;
    pageframe_t allocated_frames[num_test_frames];

    // test allocation
    for (int i = 0; i < num_test_frames; i++) {
        allocated_frames[i] = kalloc_frame();

        if (allocated_frames[i] == (PAGE_FRAME_INVALID)) {
            hcf();
        }

        for (int j = 0; j < i; j++) {
            if (allocated_frames[j] == allocated_frames[i]) {
                hcf();
            }
        }
    }

    // Test Deallocation
    for (int i = 0; i < num_test_frames; i++) {
        kfree_frame(allocated_frames[i]);
    }

    // Test re-allocation
    pageframe_t re_allocated_frame = kalloc_frame();
    if (re_allocated_frame != allocated_frames[0]) {
        hcf();
    }
}

void kmain(void)
{
    gdt_init();
    idt_init();
    pic_init();
    keyboard_init();
    pit_init(100);

    if (LIMINE_BASE_REVISION_SUPPORTED == false)
    {
        hcf();
    }

    // check if we have memory map from bootloader
    if (memmap_request.response == NULL)
    {
        hcf();
    }

    uint64_t highest_addr = 0;
    for (uint64_t i = 0; i < memmap_request.response->entry_count; i++)
    {
        struct limine_memmap_entry *entry = memmap_request.response->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE)
        {
            uint64_t top = entry->base + entry->length;
            if (top > highest_addr) highest_addr = top;
        }
    }

    init_frame_allocator(highest_addr);

    test_pfa();

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

    hcf();
}
