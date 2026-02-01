#include "vfs.h"
#include "mem/kmalloc.h"
#include "drivers/serial.h"
#include "dev.h"
#include "../string.h"

#define MAX_MOUNTPOINTS 4

typedef struct
{
    char path[0x40];
    vfs_node_t *root;
} mount_point_t;

static mount_point_t g_mounts[MAX_MOUNTPOINTS];
static int8_t g_mount_count = 0;

void vfs_init()
{
    g_mount_count = 0;
    for (int8_t i = 0; i < MAX_MOUNTPOINTS; i++)
    {
        g_mounts[i].path[0] = 0;
        g_mounts[i].root = NULL;
    }
}

int vfs_mount(const char *path, vfs_node_t *fs_root)
{
    if (g_mount_count >= MAX_MOUNTPOINTS)
    {
        kprint("VFS: Mount table full!\n");
        return -1;
    }

    strcpy(g_mounts[g_mount_count].path, path);
    g_mounts[g_mount_count++].root = fs_root;

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

file_handle_t *vfs_open(const char *filename, uint32_t mode)
{
    if (g_mount_count == 0)
    {
        kprint("VFS: No file systems mounted!\n");
        return NULL;
    }

    if (strncmp(filename, "/dev/", 5) == 0)
    {
        vfs_node_t *node = dev_find(&filename[5]);
        if (node == NULL)
        {
            kprint("Node not found!\n");
            return NULL;
        }

        file_handle_t *fhandle = kmalloc(sizeof(file_handle_t));
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

    vfs_node_t *node = vfs_navigate(filename);
    if (node == NULL)
    {
        // if ((mode & O_CREAT) && (g_fs_root->ops->create))
        // {
        //     kprint("Creating node for file: ");
        //     kprint(filename);
        //     kprint("\n");

        //     node = g_fs_root->ops->create(g_fs_root, filename, mode);
        //     if (node == NULL)
        //     {
        //         kprint("Node not found and cannot be created!\n");
        //         return NULL;
        //     }
        // }
        // else
        // {
        kprint("VFS: Node not found!\n");
        return NULL;
        // }
    }

    if (node->ops && node->ops->open)
    {
        node->ops->open(node, mode);
    }

    file_handle_t *fhandle = kmalloc(sizeof(file_handle_t));
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

uint64_t vfs_read(file_handle_t *file, uint64_t size, uint8_t *buffer)
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

uint64_t vfs_write(file_handle_t *file, uint64_t size, uint8_t *buffer)
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

void vfs_close(file_handle_t *file)
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

    if (file->node && (file->node->flags & VFS_NODE_AUTOFREE))
    {
        kfree((void *)file->node);
    }

    kfree((void *)file);
}

void vfs_seek(file_handle_t *file, uint64_t new_offset)
{
    file->offset = new_offset;
}

/**
 * @brief Path Walking to return the target vfs_node
 */
vfs_node_t *vfs_navigate(const char *path)
{
    vfs_node_t *best_mount_root = NULL;
    const char *best_mount_path = "";
    int best_match_len = -1;

    /*
    Firstly, find the best match Mount Point
    */
    for (int8_t i = 0; i < g_mount_count; i++)
    {
        // if mount = "/data", path is "/data/test.txt" -> matched!
        int mount_len = strlen(g_mounts[i].path);

        if (strncmp(path, g_mounts[i].path, mount_len) == 0)
        {
            /*
            in case mount="/d", path="/data"
            but this is wrong.
            so we check if the next char is "/", or "\0".
            */
            char next_char = path[mount_len];
            if (next_char == '/' || next_char == '\0' || mount_len == 1)
            {
                if (mount_len > best_match_len)
                {
                    best_match_len = mount_len;
                    best_mount_root = g_mounts[i].root;
                    best_mount_path = g_mounts[i].path;
                }
            }
        }
    }

    if (best_mount_root == NULL)
    {
        return NULL;
    }

    /*
    Secondly, parse the remaining Relative Path
    e.g. path="/data/TEST.TXT", best_mount="/data"
    -> relative="/TEST.TXT"
    */
    const char *rel_path = path + best_match_len;

    // rel_path is empty, or just "/", return the root right away
    if (*rel_path == '\0')
    {
        return best_mount_root;
    }

    /*
    Thirdly, find the node, starting from best_mount_root.
    */
    vfs_node_t *curr_node = best_mount_root;
    char name[128];
    int i = 0;
    while (rel_path[i] != '\0')
    {
        // skip the leading consecutive '/' (e.g. ///data/file.txt)
        while (rel_path[i] == '/')
        {
            i++;
        }

        if (rel_path[i] == '\0') // we're done
        {
            break;
        }

        // extract the next node's name
        // for example: "data" from data/test.txt
        int j = 0;
        while (rel_path[i] != '/' && rel_path[i] != '\0')
        {
            if (j < 127)
            {
                name[j++] = rel_path[i++];
            }
        }
        name[j] = '\0';

        if (curr_node->flags != VFS_DIRECTORY)
        {
            kprint("VFS: Not a directory, cannot navigate further.\n");
            return NULL;
        }

        if (curr_node->ops->finddir == NULL)
        {
            return NULL;
        }

        vfs_node_t *next_node = curr_node->ops->finddir(curr_node, name);
        if (next_node == NULL)
        {
            return NULL;
        }

        curr_node = next_node;
    }

    return curr_node;
}

int vfs_readdir(vfs_node_t *node, uint32_t index, dirent_t *out)
{
    if (node == NULL || node->ops == NULL || node->ops->readdir == NULL)
    {
        return -1;
    }

    if ((node->flags & VFS_DIRECTORY) == 0)
    {
        return -1;
    }

    return node->ops->readdir(node, index, out);
}