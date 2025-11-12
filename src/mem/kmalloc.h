#ifndef KMALLOC_H
#define KMALLOC_H

#include <stddef.h>
#include <stdint.h>

/**
 * We'll use Singly Linked List to 
 * manage the free blocks of memory
 */

typedef struct FreeBlock
{
    size_t size;
    struct FreeBlock* next;
} FreeBlock;

void* kmalloc(size_t size);
void kfree(void* blk_to_free);

#endif