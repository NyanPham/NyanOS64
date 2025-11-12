#include <stddef.h>
#include <stdint.h>

#include "kmalloc.h"
#include "pmm.h"
#include "vmm.h"

static FreeBlock* g_free_list_head = NULL;

// Request a new page frame and update the g_free_list_head
void* request_new_page()
{
    FreeBlock* new_page = pmm_alloc_frame();

    if (new_page == NULL)
    {
        return NULL;
    }

    new_page->next = g_free_list_head;
    new_page->size = PAGE_SIZE;
    g_free_list_head = new_page;

    return new_page;
}

// Walks through the free list to take the free block out
// The block must be enough size for both data and header (FreeBlock)
FreeBlock* find_free_block(size_t size)
{
    FreeBlock* prev_blk = NULL;
    FreeBlock* cur_blk = g_free_list_head;
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
void* kmalloc(size_t size)
{
    if (size >= (PAGE_SIZE - sizeof(FreeBlock)))
    {
        return NULL; // We'll handle the edge case of allocating larger size later.
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
    void* requested = (uint8_t*)blk + sizeof(FreeBlock);
    
    if (remaining_size >= sizeof(FreeBlock))
    {
        FreeBlock* remaining = (FreeBlock*)((uint8_t*)blk + allocated_size);
        remaining->size = remaining_size;
        remaining->next = g_free_list_head;
        g_free_list_head = remaining;
    }
    
    return requested;
}

void kfree(void* ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    FreeBlock* blk_to_free = (FreeBlock*)((void*)ptr - sizeof(FreeBlock));
    blk_to_free->next = g_free_list_head;
    g_free_list_head = blk_to_free;
}