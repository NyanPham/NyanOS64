#include "shm.h"

#include "../string.h"
#include "fs/vfs.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "mem/kmalloc.h"
#include "kern_defs.h"

static SharedMem_t *g_shm_list = NULL;

/* START: VFS */
static void shm_close(vfs_node_t *node)
{
    SharedMem_t *shm = (SharedMem_t *)node->device_data;
    if (shm)
    {
        shm->ref_count--;
        // if (shm->ref_count == 0)
        // {
        //     if (shm->phys_pages != NULL)
        //     {
        //         for (uint32_t i = 0; i < shm->page_count; i++)
        //         {
        //             uint64_t phys_addr = shm->phys_pages[i];
        //             pmm_free_frame((void *)(phys_addr + hhdm_offset));
        //         }
        //         kfree(shm->phys_pages);
        //     }

        //     if (g_shm_list == shm)
        //     {
        //         g_shm_list = shm->next;
        //     }
        //     else
        //     {
        //         SharedMem_t *curr = g_shm_list;
        //         while (curr != NULL && curr->next != shm)
        //         {
        //             curr = curr->next;
        //         }
        //         if (curr != NULL)
        //         {
        //             curr->next = shm->next;
        //         }
        //     }
        //     kfree(shm);
        //     node->device_data = NULL;
        // }
    }
}

vfs_fs_ops_t shm_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = shm_close,
};
/* END: VFS */

SharedMem_t *shm_get(const char *name, int flags)
{
    if (strlen(name) > 63)
    {
        kprint("SHM_GET failed: name is longer than 63 bytes\n");
        return NULL;
    }

    SharedMem_t *curr_shm = g_shm_list;
    while (curr_shm != NULL) // pointer chasing
    {
        if (strcmp(curr_shm->name, name) == 0)
        {
            return curr_shm;
        }

        curr_shm = curr_shm->next;
    }

    if (flags & O_CREAT)
    {
        SharedMem_t *new_shm = (SharedMem_t *)kmalloc(sizeof(SharedMem_t));
        if (new_shm == NULL)
        {
            kprint("SHM_GET failed: OOM\n");
            return NULL;
        }

        strcpy(new_shm->name, name);
        new_shm->size = 0;
        new_shm->phys_pages = NULL;
        new_shm->page_count = 0;
        new_shm->ref_count = 0;

        new_shm->next = g_shm_list;
        g_shm_list = new_shm;

        return new_shm;
    }

    kprint("SHM_GET: SHM node not found\n");
    return NULL;
}

int shm_set_size(SharedMem_t *shm, uint32_t new_size)
{
    if (shm->size > 0)
    {
        kprint("SHM_GET_SIZE: resizing not supported yet\n");
        return -1;
    }

    uint32_t num_pages = (new_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t *page_list = (uint64_t *)kmalloc(num_pages * sizeof(uint64_t));

    if (page_list == NULL)
    {
        return -1;
    }

    for (int i = 0; i < num_pages; i++)
    {
        void *hhdm_addr = pmm_alloc_frame();
        if (hhdm_addr == NULL)
        {
            kprint("SHM: OOM during shm_set_size\n");
            for (int j = 0; j < i; j++)
            {
                pmm_free_frame(vmm_phys_to_hhdm(page_list[j]));
            }
            kfree(page_list);
            return -1;
        }
        memset(hhdm_addr, 0, PAGE_SIZE);
        uint64_t phys_addr = vmm_hhdm_to_phys(hhdm_addr);
        page_list[i] = phys_addr;
    }

    shm->size = new_size;
    shm->page_count = num_pages;
    shm->phys_pages = page_list;

    return 0;
}

vfs_node_t *shm_create_vfs_node(const char *name, int flags)
{
    SharedMem_t *shm = shm_get(name, flags);
    if (shm == NULL)
    {
        kprint("SHM_CREATE_VFS_NODE failed to get SHM\n");
        return NULL;
    }
    shm->ref_count++;

    vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (node == NULL)
    {
        kprint("SHM_CREATE_VFS_NODE failed: OOM\n");
        return NULL;
    }
    memset(node, 0, sizeof(vfs_node_t));

    strncpy(node->name, name, strlen(name));
    node->flags = flags;
    node->length = 0;
    node->ops = &shm_ops;
    node->next = NULL;
    node->device_data = (void *)shm;

    return node;
}