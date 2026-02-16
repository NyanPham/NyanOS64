#ifndef SHM_H
#define SHM_H

#include "fs/vfs.h"
#include <stdint.h>

extern vfs_fs_ops_t shm_ops;

typedef struct SharedMem
{
    char name[64];
    uint64_t *phys_pages;
    uint32_t page_count;
    uint32_t size;
    uint32_t ref_count;
    struct SharedMem *next;
} SharedMem_t;

SharedMem_t *shm_get(const char *name, int flags);
int shm_set_size(SharedMem_t *shm, uint32_t new_size);
vfs_node_t *shm_create_vfs_node(const char *name, int flags);

#endif