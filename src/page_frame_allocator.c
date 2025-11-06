#include <stddef.h>
#include "page_frame_allocator.h"

extern uint64_t endkernel;

static pageframe_t pre_frames[PRE_ALLOC_SIZE];

uint8_t* frame_map = NULL;
uint64_t npages = 0;
uint64_t startframe = 0;
uint64_t memory_size = 0;

void init_frame_allocator(uint64_t total_memory)
{
    startframe = (((uint64_t)&endkernel + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
    memory_size = total_memory;
    npages = memory_size / PAGE_SIZE;
    frame_map = (uint8_t*)startframe;

    uint64_t map_bytes = npages;
    uint64_t map_pages = (map_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    uint64_t kernel_frames = startframe / PAGE_SIZE;
    if (kernel_frames + map_pages > npages) {
        frame_map = NULL;
        npages = 0;
        return;
    }

    for (uint64_t i = 0; i < npages; i++) 
    {
        frame_map[i] = FREE;
    }

    uint64_t reserved = kernel_frames + map_pages;
    if (reserved > npages) reserved = npages;
    for (uint64_t i = 0; i < reserved; i++)
    {
        frame_map[i] = USED;
    }
}

static pageframe_t kalloc_frame_int(void)
{
    if (frame_map == NULL)
    {
        return (ERROR);
    }

    for (uint64_t i = 0; i < npages; i++) {
        if (frame_map[i] == FREE) {
            frame_map[i] = USED;
            return (pageframe_t)(i * PAGE_SIZE);
        }
    }
    return ERROR;
}

pageframe_t kalloc_frame(void)
{
    static uint8_t allocate = 1;
    static uint8_t pframe = 0;
    pageframe_t ret;

    if (pframe == PRE_ALLOC_SIZE)
    {
        allocate = 1;
    }

    if (allocate == 1)
    {
        for (int i = 0; i < PRE_ALLOC_SIZE; i++)
        {
            pre_frames[i] = kalloc_frame_int(); // <-- Problem is here
        }
        pframe = 0;
        allocate = 0;
    }
    ret = pre_frames[pframe];
    pframe++;
    return ret;
}


void kfree_frame(pageframe_t a)
{
    if (a == PAGE_FRAME_INVALID)
    {
        return;
    }

    if (a % PAGE_SIZE != 0) 
    {
        return;
    }

    uint64_t index = (uint64_t)(a / PAGE_SIZE);
    if (index >= npages) return;
    frame_map[index] = FREE;
}