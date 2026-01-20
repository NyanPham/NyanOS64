#ifndef PIPE_H
#define PIPE_H

#include "vfs.h"
#include "utils/ring_buf.h"

#define READ_OPEN (1 << 0)
#define WRITE_OPEN (1 << 1)

typedef struct Pipe
{
    RingBuf buf;
    int reader_pid;
    int writer_pid;
    uint32_t flags;
} Pipe;

extern vfs_fs_ops_t pipe_read_ops;
extern vfs_fs_ops_t pipe_write_ops;

#endif