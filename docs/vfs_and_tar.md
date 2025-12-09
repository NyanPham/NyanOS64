# An upgrade from tar to VFS (POSFIX-like)

We'll build a Virtual File System (VFS) to replace our fast-food TAR reader. Why?

## Reasons:
- `syscall` gets the filename, the kernel then runs the hardcoded `tar_read_file` function.
- We want our system to be compatible to FAT32 and EXT2, and we don't want to rewrite the whole `syscall.c`.
- User-space doesn't care where the offset of the pointer to a file; they want to open, read, and close instead.

So let's build an abstraction layer that wraps our current tar_init, tar_read_file. Users only use file descriptors.

## VFS structure

1. **`vfs_node_t` (Inode)**: represents the file on physics. It has the `ops` field, which is implemented differently for different drivers.
2. **`file_handle_t` (File Description)**: a open file, with `offset` of the pointer to where the content is being read.
3. **`vfs_fs_ops_t`**: standard interface with `read`, `writer`, `open`, `close`, and `finddir`.

Two intermediate functions `vfs_open` (to find node, create handle) and `vfs_read` (verify handle, call read of the driver, and update offset) are provided.

## Wrap TAR with a Driver

TAR doesn't come with a standard driver for our system, so we'll need an *adapter*:

- **`tar_vfs_finddir`**: walk through headers 512 byte of TAR to find a file name. If found, create `vfs_node_t`, attach the file content (addr) to `device_data`.
- **`tar_vfs-read`**: read the content in `device_data`, copy into user's buffer.

Instead of calling `tar_init`, we use `vfs_init` -> `tar_fs_init()` -> `vfs_mount("/", tar_root)`.

## File descriptor
Each task has its own table of open file descriptors.
So we add `fd_tbl[16]` to `struct task`.
We:
- add `find_free_fd` to find a free slot in the table;
- update `sched_create_task` and `sched_init` to ensure all new tasks have empty fd_tbl.