#ifndef PAGE_FRAME_ALLOCATOR_H
#define PAGE_FRAME_ALLOCATOR_H

#include <stdint.h>

#define PAGE_SIZE 4096
#define PAGE_FRAME_INVALID ((pageframe_t) - 1)
#define FREE 0
#define USED 1
#define ERROR PAGE_FRAME_INVALID
#define PRE_ALLOC_SIZE 20

typedef uint64_t pageframe_t;

void init_frame_allocator(uint64_t total_memory);
pageframe_t kalloc_frame(void);
void kfree_frame(pageframe_t frame);

#endif