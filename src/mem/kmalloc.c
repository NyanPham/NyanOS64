#include <stddef.h>
#include <stdint.h>

#include "kmalloc.h"
#include "pmm.h"
#include "vmm.h"
#include "drivers/serial.h"
#include "kern_defs.h"
#include "utils/spinlock.h"

#define KERN_HEAP_END (KERN_HEAP_START + 0x10000000ULL)

extern uint64_t hhdm_offset;
static FreeBlock *g_free_list_head = NULL;
static spinlock_t kmalloc_lock = SPINLOCK_INIT;

void kfree_unlocked(void *ptr);
void kfree(void *ptr);

void *request_new_page(void)
{
    uint64_t phys_addr = pmm_alloc_frame();
    if (phys_addr == 0)
    {
        return NULL;
    }

    FreeBlock *ptr = (FreeBlock *)vmm_phys_to_hhdm(phys_addr);
    if (ptr == NULL)
    {
        return NULL;
    }

    if ((uint64_t)ptr < hhdm_offset)
    {
        ptr = vmm_phys_to_hhdm((uint64_t)ptr);
    }

    FreeBlock *new_page = (FreeBlock *)ptr;
    new_page->size = PAGE_SIZE;
    kfree_unlocked((void *)((uint8_t *)new_page + sizeof(FreeBlock)));

    return new_page;
}

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

void *kmalloc(size_t size)
{
    if (size == 0)
    {
        return NULL;
    }

    size = (size + 15) & ~(size_t)15;

    if (size >= (PAGE_SIZE - sizeof(FreeBlock)))
    {
        return vmm_alloc_global(size);
    }

    uint64_t flags;
    SPIN_LOCK_IRQSAVE(&kmalloc_lock, flags);

    FreeBlock *blk = find_free_block(size);
    if (blk == NULL)
    {
        if (request_new_page() == NULL)
        {
            SPIN_UNLOCK_IRQRESTORE(&kmalloc_lock, flags);
            return NULL;
        }
        blk = find_free_block(size);
        if (blk == NULL)
        {
            SPIN_UNLOCK_IRQRESTORE(&kmalloc_lock, flags);
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
        kfree_unlocked((void *)((uint8_t *)remaining + sizeof(FreeBlock)));
    }
    else
    {
        blk->size += remaining_size;
    }

    SPIN_UNLOCK_IRQRESTORE(&kmalloc_lock, flags);
    return ptr;
}

void kfree_unlocked(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    FreeBlock *blk_to_free = (FreeBlock *)((uint8_t *)ptr - sizeof(FreeBlock));
    if (blk_to_free->size == 0)
    {
        return;
    }

    FreeBlock *prev = g_free_list_head;

    if (g_free_list_head == NULL || (uint64_t)blk_to_free < (uint64_t)g_free_list_head)
    {
        blk_to_free->next = g_free_list_head;
        g_free_list_head = blk_to_free;

        if (blk_to_free->next != NULL &&
            ((uint64_t)blk_to_free + blk_to_free->size) == (uint64_t)blk_to_free->next)
        {
            blk_to_free->size += blk_to_free->next->size;
            blk_to_free->next = blk_to_free->next->next;
        }
        return;
    }

    while (prev->next != NULL && (uint64_t)prev->next < (uint64_t)blk_to_free)
    {
        prev = prev->next;
    }

    blk_to_free->next = prev->next;
    prev->next = blk_to_free;

    if (blk_to_free->next != NULL &&
        ((uint64_t)blk_to_free + blk_to_free->size) == (uint64_t)blk_to_free->next)
    {
        blk_to_free->size += blk_to_free->next->size;
        blk_to_free->next = blk_to_free->next->next;
    }

    if (((uint64_t)prev + prev->size) == (uint64_t)blk_to_free)
    {
        prev->size += blk_to_free->size;
        prev->next = blk_to_free->next;
    }
}

void kfree(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    uint64_t addr = (uint64_t)ptr;
    if (addr >= KERN_HEAP_START && addr < KERN_HEAP_END)
    {
        vmm_free(ptr);
        return;
    }

    uint64_t flags;
    SPIN_LOCK_IRQSAVE(&kmalloc_lock, flags);
    kfree_unlocked(ptr);
    SPIN_UNLOCK_IRQRESTORE(&kmalloc_lock, flags);
}