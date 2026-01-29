#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>

#define VFS_FILE 0x01
#define VFS_DIRECTORY 0x02
#define VFS_CHAR_DEVICE 0x03
#define VFS_BLOCK_DEVICE 0x04
#define VFS_NODE_AUTOFREE 0x10

#define O_RDONLY 0x0
#define O_WRONLY 0x1
#define O_RDWR 0x2
#define O_CREAT 0x40
#define O_EXCL 0x80
#define O_TRUNC 0x200
#define O_APPEND 0x400

struct vfs_node;

typedef struct vfs_fs_ops
{
    uint64_t (*read)(struct vfs_node *node, uint64_t offset, uint64_t size, uint8_t *buffer);
    uint64_t (*write)(struct vfs_node *node, uint64_t offset, uint64_t size, uint8_t *buffer);
    void (*open)(struct vfs_node *node, uint32_t mode);
    void (*close)(struct vfs_node *node);
    struct vfs_node *(*finddir)(struct vfs_node *node, const char *name);
    struct vfs_node *(*create)(struct vfs_node *parent, const char *name, uint32_t flags);
} vfs_fs_ops_t;

typedef struct vfs_node
{
    char name[128];
    uint32_t flags;
    uint64_t length;

    vfs_fs_ops_t *ops;
    void *device_data;
    struct vfs_node *next;
} vfs_node_t;

typedef struct file_handle
{
    vfs_node_t *node;
    uint64_t offset;
    uint32_t mode;
    int ref_count;
} file_handle_t;

void vfs_init();
int vfs_mount(const char *path, vfs_node_t *fs_root);
void vfs_retain(file_handle_t *file);
file_handle_t *vfs_open(const char *filename, uint32_t mode);
void vfs_close(file_handle_t *file);
uint64_t vfs_read(file_handle_t *file, uint64_t size, uint8_t *buffer);
uint64_t vfs_write(file_handle_t *file, uint64_t size, uint8_t *buffer);
void vfs_seek(file_handle_t *file, uint64_t new_offset);

#endif