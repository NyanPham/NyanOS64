#include "vfs.h"
#include "tar.h"
#include "mem/kmalloc.h"
#include "./string.h"
#include "drivers/serial.h"

uint64_t tar_vfs_read(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
vfs_node_t *tar_vfs_finddir(vfs_node_t *node, const char *name);
int tar_vfs_readdir(vfs_node_t *node, uint32_t idx, dirent_t *out);

static tar_header *g_tar_base_addr = NULL;

static vfs_fs_ops_t tar_ops =
    {
        .read = tar_vfs_read,
        .finddir = tar_vfs_finddir,
        .open = NULL, // tar needs no special open/close
        .close = NULL,
        .write = NULL, // and tar is read-only
        .create = NULL,
        .readdir = tar_vfs_readdir,
};

// create root node for TAR
vfs_node_t *tar_fs_init(void *tar_addr)
{
    g_tar_base_addr = (tar_header *)tar_addr;
    tar_init(tar_addr);

    vfs_node_t *root = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    strcpy(root->name, "/");
    root->flags = VFS_DIRECTORY;
    root->length = 0;
    root->ops = &tar_ops;
    root->device_data = tar_addr;

    return root;
}

uint64_t tar_vfs_read(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    // we'll store the tar's addr into the device_data

    char *f_content = (char *)node->device_data;
    uint64_t f_size = node->length;

    if (offset >= f_size)
    {
        return 0;
    }
    if (offset + size > f_size)
    {
        size = f_size - offset;
    }

    char *src = f_content + offset;
    for (uint64_t i = 0; i < size; i++)
    {
        buffer[i] = src[i];
    }

    return size;
}

vfs_node_t *tar_vfs_finddir(vfs_node_t *node, const char *name)
{
    void *tar_addr = node->device_data;
    tar_header *hdr = (tar_header *)tar_addr;

    while (1)
    {
        if (!tar_validate(hdr))
        {
            break;
        }

        if (strcmp(hdr->name, name) == 0)
        {
            char *content = (char *)((uint64_t)hdr + 512);
            vfs_node_t *_node = kmalloc(sizeof(vfs_node_t));

            if (strlen(name) >= 128)
            {
                kprint("Name is too long\n");
                return NULL;
            }

            strcpy(_node->name, name);
            _node->length = oct2bin(hdr->size, 11);
            _node->flags = (hdr->typeflag == '5') ? VFS_DIRECTORY : VFS_FILE;
            _node->ops = &tar_ops;
            _node->device_data = content;
            return _node;
        }

        uint64_t size = oct2bin(hdr->size, 11);
        uint64_t size_aligned = (size + 511) / 512 * 512;
        uint64_t next_hdr_addr = (uint64_t)hdr + 512 + size_aligned;
        hdr = (tar_header *)next_hdr_addr;
    }

    return NULL;
}

int tar_vfs_readdir(vfs_node_t *node, uint32_t idx, dirent_t *out)
{
    if (g_tar_base_addr == NULL)
    {
        return -1;
    }

    tar_header *header = g_tar_base_addr;
    uint32_t curr_idx = 0;

    while (header->name[0] != '\0')
    {
        uint64_t size = oct2bin(header->size, 11);
        uint64_t size_aligned = (size + 511) / 512 * 512;

        if (curr_idx == idx)
        {
            strcpy(out->name, header->name);

            if (header->typeflag == '5' || (strlen(header->name) > 0 && header->name[strlen(header->name) - 1] == '/'))
            {
                out->type = VFS_DIRECTORY;
                int len = strlen(out->name);
                if (out->name[len - 1] == '/')
                {
                    out->name[len - 1] = 0;
                }
            }
            else
            {
                out->type = VFS_FILE;
            }
            out->size = size;
            return 0;
        }
        curr_idx++;
        header = (tar_header *)((uint64_t)header + 512 + size_aligned);
    }

    return -1;
}