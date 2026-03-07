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

        if (mode & O_TRUNC)
        {
            node->length = 0;
        }

        file_handle_t *fhandle = kmalloc(sizeof(file_handle_t));
        if (fhandle == NULL)
        {
            kprint("vfs_open failed: out of mem!\n");
            return NULL;
        }
        fhandle->node = node;
        fhandle->mode = mode;
        fhandle->ref_count = 1;
        fhandle->offset = (mode & O_APPEND)
                              ? node->length
                              : 0;

        return fhandle;
    }

    vfs_node_t *node = vfs_navigate(filename);
    if (node == NULL)
    {
        if (mode & O_CREAT)
        {
            /*
            First, determine the parent and child
            For example, filename = "/data/test.txt"
            parent = "/data" and child = "test.txt"
            */
            char parent_path[128];
            char child_name[128];

            int len = strlen(filename);
            int last_slash = -1;
            for (int i = len - 1; i >= 0; i--)
            {
                if (filename[i] == '/')
                {
                    last_slash = i;
                    break;
                }
            }

            if (last_slash == -1)
            {
                // no parent, then it's at root
                // like "test.txt"
                return NULL;
            }

            strncpy(parent_path, filename, last_slash);
            parent_path[last_slash] = '\0';
            if (last_slash == 0)
            {
                strcpy(parent_path, "/");
            }
            strcpy(child_name, &filename[last_slash + 1]);

            vfs_node_t *parent_node = vfs_navigate(parent_path);

            if (parent_node == NULL)
            {
                kprint("VFS_OPEN failed: Parent directory not found for creation.\n");
                return NULL;
            }

            if (parent_node->ops && parent_node->ops->create)
            {
                vfs_node_t *created = parent_node->ops->create(parent_node, child_name, mode);
                if (created == NULL)
                {
                    node = vfs_navigate(filename);
                }
            }

            if (node == NULL)
            {
                kprint("VFS_OPEN failed: Node not found after attempt to create\n");
                return NULL;
            }
        }
        else
        {
            kprint("VFS: Node not found!\n");
            return NULL;
        }
    }

    if (node->ops && node->ops->open)
    {
        node->ops->open(node, mode);
    }

    if (mode & O_TRUNC)
    {
        node->length = 0;
    }

    file_handle_t *fhandle = kmalloc(sizeof(file_handle_t));
    if (fhandle == NULL)
    {
        kprint("vfs_open failed: out of mem!\n");
        return NULL;
    }

    fhandle->node = node;
    fhandle->mode = mode;
    fhandle->ref_count = 1;
    fhandle->offset = (mode & O_APPEND)
                          ? node->length
                          : 0;

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
    // const char *best_mount_path = "";
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
                    // best_mount_path = g_mounts[i].path;
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

int vfs_unlink(const char *path)
{
    vfs_node_t *node = vfs_navigate(path);
    if (node == NULL)
    {
        return -1;
    }

    if (node->ops && node->ops->unlink)
    {
        node->ops->unlink(node);
    }

    kfree(node);
    return 0;
}

void resolve_path(const char *cwd, const char *inp_path, char *out_buf)
{
    char *tmp = (char *)kmalloc(256);

    // First, let's find the starting point
    if (inp_path[0] == '/')
    {
        // absolute path
        strcpy(tmp, inp_path);
    }
    else
    {
        // relative path -> cwd + "/" + inp_path
        strcpy(tmp, cwd);
        int len = strlen(tmp);
        if (len > 1 && tmp[len - 1] != '/')
        {
            strcat(tmp, "/");
        }
        strcat(tmp, inp_path);
    }

    // Second, handle the Stack logic with `..` and `.`
    char (*stack)[32] = (char (*)[32])kmalloc(32 * 32);
    if (stack == NULL)
    {
        kfree(tmp);
        return;
    }
    int top = 0;
    int i = 0;

    if (tmp[0] == '/')
    {
        i = 1;
    }

    char name_buf[32];
    int n_idx = 0;

    while (1)
    {
        char c = tmp[i];

        if (c == '/' || c == 0)
        {
            name_buf[n_idx] = 0;
            if (n_idx > 0)
            {
                if (strcmp(name_buf, "..") == 0)
                {
                    if (top > 0)
                        top--;
                }
                else if (strcmp(name_buf, ".") == 0)
                {
                    // ignore
                }
                else
                {
                    strcpy(stack[top++], name_buf);
                }
            }
            n_idx = 0;
            if (c == 0)
            {
                break;
            }
        }
        else
        {
            name_buf[n_idx++] = c;
        }
        i++;
    }

    // Last, rebuild the stack path
    strcpy(out_buf, "/");
    for (int k = 0; k < top; k++)
    {
        if (k > 0)
        {
            strcat(out_buf, "/");
        }
        strcat(out_buf, stack[k]);
    }

    kfree(tmp);
    kfree(stack);
}