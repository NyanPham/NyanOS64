/**
 * @file pmm.c
 * @brief A bitmap-based Physical Memory Manager (PMM).
 *
 * The PMM is responsible for one of the most critical tasks in the kernel:
 * managing the machine's physical memory. It allows the kernel to allocate
 * and free physical memory in fixed-size chunks called "frames" (or "pages").
 *
 * This implementation uses a bitmap, which is a simple and efficient way to
 * track the status of every single page frame. Each bit in the bitmap corresponds
 * to a physical page: if the bit is 1, the page is used; if it's 0, it's free.
 */

#include "pmm.h"
#include "kern_defs.h"

#include <limine.h>
#include <stddef.h>

// Global variables that manage the state of our physical memory

uint64_t hhdm_offset = 0;    // the begin of the virtual address provided by limine, this is a shared by other components as well
static uint8_t* bitmap = NULL;
static uint64_t highest_addr = 0;
static size_t bitmap_size = 0;
static size_t bitmap_page_count = 0;
static size_t total_pages = 0;
static size_t last_free_page = 0;     // speed up allocation searches

/**
 * @brief Sets a bit in the bitmap to mark a page is used
 */
void bitmap_set(size_t bit) {
    bitmap[bit / 8] |= (1 << (bit % 8));
}

/**
 * @brief Clears a bit in the bitmap to mark a page is free 
 */
void bitmap_clear(size_t bit) {
    bitmap[bit / 8] &= ~(1 << (bit % 8));
}

/**
 * @brief Checks if a bit is set to see if a page is used
 */
bool bitmap_test(size_t bit) {
    return bitmap[bit / 8] & (1 << (bit % 8));
}

/**
 * @brief Finds the highest usable physical address from the memory map
 * to determine the total amount of memory to manage.
 */
static uint64_t get_memory_size(struct limine_memmap_entry** memmap, uint64_t memmap_entries_cnt) {
    uint64_t highest_addr = 0;
    for (uint64_t i = 0; i < memmap_entries_cnt; i++) {
        struct limine_memmap_entry* entry = memmap[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t top = entry->base + entry->length;
            if (top > highest_addr) {
                highest_addr = top;
            }
        }
    }
    return highest_addr;
}

/**
 * @brief Initializes the Physical Memory Manager.
 * Sets up the bitmap that tracks every pages of PM.
 */
void pmm_init(struct limine_memmap_response* memmap_resp, struct limine_hhdm_response* hhdm_resp) {
    // get the higher-half direct map (HHDM) offset from Limine.
    hhdm_offset = hhdm_resp->offset;

    struct limine_memmap_entry** memmap = memmap_resp->entries;
    uint64_t memmap_entries_cnt = memmap_resp->entry_count;

    // get total amount of memory and how many pages it possibly consists of
    highest_addr = get_memory_size(memmap, memmap_entries_cnt);
    total_pages = highest_addr / PAGE_SIZE;

    // compute the size of the bitmap needed to track all pages.
    // each bit is to track 1 page, so we divide the byte by 8 bits
    bitmap_size = total_pages / 8;
    bitmap_page_count = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;

    // find a chunk of PM that is large enough to store the bitmap
    for (uint64_t i = 0; i < memmap_entries_cnt; i++) {
        struct limine_memmap_entry* entry = memmap[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= (bitmap_page_count * PAGE_SIZE)) {
            bitmap = (uint8_t*)(entry->base + hhdm_offset);
            break;
        }
    }

    // Oops! bitmap is not stored anywhere, no spot found to store it. we failed
    if (bitmap == NULL) {
        for (;;) { __asm__("cli; hlt"); }
    }

    // marks all pages to be used
    for (size_t i = 0; i < bitmap_size; i++) {
        bitmap[i] = 0xFF;
    }

    // we iterate through each entry, check if it's usable; if yes, make it as free
    for (uint64_t i = 0; i < memmap_entries_cnt; i++) { // loop for each region
        struct limine_memmap_entry* entry = memmap[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            for (uint64_t j = 0; j < entry->length / PAGE_SIZE; j++) { // loop for each page in the current region
                bitmap_clear((entry->base / PAGE_SIZE) + j); // clear the bit, marks it free
            }
        }
    }

    // remember we store bitmap in the usable memory? we mark the page to be used as well.
    for (size_t i = 0; i < bitmap_page_count; i++) {
        bitmap_set((((uintptr_t)bitmap - hhdm_offset) / PAGE_SIZE) + i);
    }

    // reserve the first mb, to avoid conflict with BIOS, bootloader functions.
    for (size_t i = 0; i < (0x100000 / PAGE_SIZE); i++) {
        bitmap_set(i);
    }
}

/**
 * @brief Allocates a single physical memory frame.
 * @return A virtual pointer to the start of the allocated 4KB frame, or NULL if out of memory.
 */
void* pmm_alloc_frame() {
    // starts scanning from the last known free page, do not start the scanning from the beginning
    for (size_t i = last_free_page; i < total_pages; i++) {
        if (!bitmap_test(i)) { // page is free?
            bitmap_set(i);  
            last_free_page = i + 1;
            return (void*)((i * PAGE_SIZE) + hhdm_offset); // convert to a virtual address.
        }
    }

    // if the above failed, we start from the beginning. Why? there might be a page freed before the last_free_page
    for (size_t i = 0; i < last_free_page; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            last_free_page = i + 1;
            return (void*)((i * PAGE_SIZE) + hhdm_offset);
        }
    }

    return NULL; // mem is full
}

/**
 * @brief Frees a previously allocated physical memory frame.
 * @param frame_address The virtual address of the frame to free.
 */
void pmm_free_frame(void* frame_address) {
    // convert back from VM addr to PM addr, and compute the bit index in the bitmap
    size_t bit = ((uintptr_t)frame_address - hhdm_offset) / PAGE_SIZE; 
    if (bitmap_test(bit)) {
        // mark it as free
        bitmap_clear(bit);
        if (bit < last_free_page) {
            last_free_page = bit;
        }
    }
}
