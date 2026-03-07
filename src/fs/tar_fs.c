#include "vfs.h"
#include "tar.h"
#include "mem/kmalloc.h"
#include "./string.h"
#include "fs/tar.h"
#include "drivers/serial.h"

uint64_t tar_vfs_read(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
vfs_node_t *tar_vfs_finddir(vfs_node_t *node, const char *name);
int tar_vfs_readdir(vfs_node_t *node, uint32_t idx, dirent_t *out);

static tar_header_t *g_tar_base_addr = NULL;
static char *g_tar_root_path = "";

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
    g_tar_base_addr = (tar_header_t *)tar_addr;
    tar_init(tar_addr);

    vfs_node_t *root = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    strcpy(root->name, "/");
    root->flags = VFS_DIRECTORY;
    root->length = 0;
    root->ops = &tar_ops;
    root->device_data = g_tar_root_path;

    return root;
}

uint64_t tar_vfs_read(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    if (node->flags & VFS_DIRECTORY)
    {
        return 0;
    }

    // we'll store the tar's header into the device_data
    tar_header_t *hdr = (tar_header_t *)node->device_data;
    char *f_content = (char *)((uint64_t)hdr + 512);
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
    // void *tar_addr = node->device_data;
    // tar_header_t *hdr = (tar_header_t *)tar_addr;

    // for the root, it's g_tar_root_path
    // for child, it's header->name, the first field
    char *parent_path = (char *)node->device_data;
    char search_path[256];
    memset(search_path, 0, 256);
    strcpy(search_path, parent_path);
    strcat(search_path, name);

    if (g_tar_base_addr == NULL)
    {
        return NULL;
    }

    tar_header_t *iter = g_tar_base_addr;

    while (1)
    {
        if (!tar_validate(iter))
        {
            break;
        }

        uint8_t match = 0;
        if (strcmp(iter->name, search_path) == 0)
        {
            match = 1;
        }
        else
        {
            // search_path is "usr, and in TAR it's "usr/"
            int len = strlen(search_path);
            if (strncmp(iter->name, search_path, len) == 0 && iter->name[len] == '/' && iter->name[len + 1] == '\0')
            {
                match = 1;
            }
        }

        if (match)
        {
            vfs_node_t *_node = kmalloc(sizeof(vfs_node_t));

            if (strlen(name) >= 128)
            {
                kprint("Name is too long\n");
                kfree(_node);
                return NULL;
            }

            strcpy(_node->name, name);
            _node->length = oct2bin(iter->size, 11);
            int name_len = strlen(iter->name);
            _node->flags = (iter->typeflag == '5' || (name_len > 0 && iter->name[name_len - 1] == '/'))
                               ? VFS_DIRECTORY
                               : VFS_FILE;
            _node->ops = &tar_ops;
            _node->device_data = iter;
            return _node;
        }

        uint64_t size = oct2bin(iter->size, 11);
        uint64_t size_aligned = (size + 511) / 512 * 512;
        uint64_t next_hdr_addr = (uint64_t)iter + 512 + size_aligned;
        iter = (tar_header_t *)next_hdr_addr;
    }

    return NULL;
}

int tar_vfs_readdir(vfs_node_t *node, uint32_t idx, dirent_t *out)
{
    if (g_tar_base_addr == NULL)
    {
        return -1;
    }

    char *dir_path = (char *)node->device_data;
    int dir_len = strlen(dir_path);

    tar_header_t *header = g_tar_base_addr;
    uint32_t curr_idx = 0;

    while (header->name[0] != '\0')
    {

        uint64_t size = oct2bin(header->size, 11);
        uint64_t size_aligned = (size + 511) / 512 * 512;
        tar_header_t *next_header = (tar_header_t *)((uint64_t)header + 512 + size_aligned);

        if (strncmp(header->name, dir_path, dir_len) != 0)
        {
            header = next_header;
            continue;
        }

        char *suffix = header->name + dir_len;
        if (strlen(suffix) == 0)
        {
            header = next_header;
            continue;
        }

        char *slash = strchr(suffix, '/');
        if (slash != NULL && slash[1] != '\0')
        {
            header = next_header;
            continue;
        }

        if (curr_idx == idx)
        {
            strcpy(out->name, header->name + dir_len);

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
        header = next_header;
    }

    return -1;
}