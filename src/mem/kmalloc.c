#include <stddef.h>
#include <stdint.h>

#include "kmalloc.h"
#include "pmm.h"
#include "vmm.h"
#include "drivers/serial.h" // debugging

extern uint64_t hhdm_offset;

static FreeBlock *g_free_list_head = NULL;

void kfree(void *ptr);

// Request a new page frame and update the g_free_list_head
void *request_new_page()
{
    FreeBlock *ptr = pmm_alloc_frame();

    if (ptr == NULL)
    {
        return NULL;
    }

    // we need to convertthe phys_addr to virt_addr
    if ((uint64_t)ptr < hhdm_offset)
    {
        ptr = (void*)((uint64_t)ptr + hhdm_offset);
    }

    FreeBlock *new_page = (FreeBlock*)ptr;
    new_page->size = PAGE_SIZE;
    kfree((void*)((uint8_t*)new_page + sizeof(FreeBlock)));

    return new_page;
}

// Walks through the free list to take the free block out
// The block must be enough size for both data and header (FreeBlock)
FreeBlock *find_free_block(size_t size)
{
    FreeBlock *prev_blk = NULL;
    FreeBlock *cur_blk = g_free_list_head;

    while (cur_blk != NULL)
    {
        if (cur_blk->size >= size + sizeof(FreeBlock))
        {
            if (prev_blk == NULL)
            {
                g_free_list_head = cur_blk->next;
            }
            else
            {
                prev_blk->next = cur_blk->next;
            }
            cur_blk->next = NULL;
            return cur_blk;
        }
        prev_blk = cur_blk;
        cur_blk = cur_blk->next;
    }

    return NULL;
}

/**
 * Allocate space for the requested size.
 * If the requested size is larger than 4096 + header size,
 * we'll handle them differently, later...
 * Otherwise, we find the free block from the list.
 * If not found, request the block.
 * The returned address is the found block + header size.
 * If the remaining is large enough for header, we split the block,
 * and return the remaining free space to the list.
 */
void *kmalloc(size_t size)
{
    if (size == 0)
    {
        return NULL;
    }

    // round up to multiple of 8
    size = (size + 7) & ~7;

    if (size >= (PAGE_SIZE - sizeof(FreeBlock)))
    {
        return NULL; // TODO: we'll handle the edge case of allocating larger size later.
    }

    FreeBlock *blk = find_free_block(size);
    if (blk == NULL)
    {
        if (request_new_page() == NULL)
        {
            return NULL;
        }
        blk = find_free_block(size);
        if (blk == NULL)
        {
            return NULL;
        }
    }
    
    size_t allocated_size = sizeof(FreeBlock) + size;
    size_t remaining_size = blk->size - allocated_size;
    blk->size = allocated_size;
    void *ptr = (uint8_t *)blk + sizeof(FreeBlock);

    if (remaining_size >= sizeof(FreeBlock))
    {
        FreeBlock *remaining = (FreeBlock *)((uint8_t *)blk + allocated_size);
        remaining->size = remaining_size;
        kfree((void*)((uint8_t*)remaining + sizeof(FreeBlock)));
    }
    else 
    {
        blk->size += remaining_size;
    }

    return ptr;
}

void kfree(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    FreeBlock *blk_to_free = (FreeBlock *)((void *)ptr - sizeof(FreeBlock));
    
    // coalescing
    if (blk_to_free->size == 0)
    {
        return;
    }

    // 1. find the predecessor
    FreeBlock *prev = g_free_list_head;

    // case 1:  in head of the list
    if (g_free_list_head == NULL || (uint64_t)blk_to_free < (uint64_t)g_free_list_head)
    {
        blk_to_free->next = g_free_list_head;
        g_free_list_head = blk_to_free;

        // merge right
        if (blk_to_free->next != NULL && 
        ((uint64_t)blk_to_free + blk_to_free->size) == (uint64_t)blk_to_free->next)
        {
            blk_to_free->size += blk_to_free->next->size;
            blk_to_free->next = blk_to_free->next->next;
        }
        return;
    }

    // case 2: insert in the middle, or end
    while (prev->next != NULL && (uint64_t)prev->next < (uint64_t)blk_to_free)
    {
        prev = prev->next;
    }

    // 2. insert the blk_to_free after pred
    blk_to_free->next = prev->next;
    prev->next = blk_to_free;

    // 3. merge right - blk_to_free /w next?
    if (blk_to_free->next != NULL && 
       ((uint64_t)blk_to_free + blk_to_free->size) == (uint64_t)blk_to_free->next)
    {
        blk_to_free->size += blk_to_free->next->size;
        blk_to_free->next = blk_to_free->next->next;
    }

    // 4. merge left - pred /w blk_to_free
    if (((uint64_t)prev + prev->size) == (uint64_t)blk_to_free)
    {
        prev->size += blk_to_free->size;
        prev->next = blk_to_free->next;
    }
}