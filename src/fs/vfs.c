#include "vfs.h"
#include "mem/kmalloc.h"
#include "drivers/serial.h"
#include "../string.h"

static vfs_node_t* g_fs_root = NULL;

void vfs_init()
{
    g_fs_root = NULL;
}

int vfs_mount(const char* path, vfs_node_t* fs_root)
{
    g_fs_root = fs_root;
    return 0;
}

void vfs_retain(file_handle_t *file)
{
    if (file == NULL)
    {
        return;
    }
    file->ref_count++;
}

file_handle_t* vfs_open(const char* filename, uint32_t mode)
{
    if (g_fs_root == NULL)
    {
        kprint("VFS not mounted!\n");
        return NULL;
    }

    if (!(g_fs_root->ops && g_fs_root->ops->finddir))
    {
        kprint("VFS not having ops finddir!\n");
        return NULL;
    }

    vfs_node_t* node = g_fs_root->ops->finddir(g_fs_root, filename);
    if (node == NULL)
    {
        kprint("Node not found!\n");
        return NULL;
    }

    if (node->ops && node->ops->open)
    {
        node->ops->open(node);
    }

    file_handle_t* fhandle = kmalloc(sizeof(file_handle_t));
    if (fhandle == NULL)
    {
        kprint("vfs_open failed: out of mem!\n");
        return NULL;
    }
    
    fhandle->node = node;
    fhandle->offset = 0;
    fhandle->mode = mode;
    fhandle->ref_count = 1;

    return fhandle;
}

uint64_t vfs_read(file_handle_t* file, uint64_t size, uint8_t* buffer)
{
    if (file == NULL)
    {
        kprint("VFS READ: Invalid file\n"); 
        return 0;
    }

    if (file->node && file->node->ops && file->node->ops->read)
    {
        uint64_t nbytes = file->node->ops->read(file->node, file->offset, size, buffer);
        file->offset += nbytes;
        return nbytes;
    }

    return 0;
}

uint64_t vfs_write(file_handle_t* file, uint64_t size, uint8_t* buffer)
{
    if (file == NULL)
    {
        kprint("VFS WRITE: Invalid file\n"); 
        return 0;
    }
    
    if (file->node && file->node->ops && file->node->ops->write)
    {
        uint64_t nbytes = file->node->ops->write(file->node, file->offset, size, buffer);
        file->offset += nbytes;
        return nbytes;
    }

    return 0;
}

void vfs_close(file_handle_t* file)
{
    if (file == NULL)
    {
        kprint("VFS CLOSE: Invalid file\n"); 
        return;
    }

    file->ref_count--;

    if (file->ref_count > 0)
    {
        return;
    }

    if (file->node && file->node->ops && file->node->ops->close)
    {
        file->node->ops->close(file->node);
    }

    if (file->node && file->node != g_fs_root && (file->node->flags & VFS_NODE_AUTOFREE)) 
    {
        kfree((void*)file->node);
    }

    kfree((void*)file);
}