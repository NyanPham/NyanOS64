#include "pipe.h"
#include "sched/sched.h"
#include "utils/asm_instrs.h"

uint64_t pipe_read(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buf)
{
    Pipe *pipe = (Pipe *)node->device_data;
    uint64_t read_count = 0;

    while (read_count < size)
    {
        cli();
        char c;

        /*
        We get the contents from the RingBuf one by one
        until the RingBuf is empty, that's when the write_end is closed.
        */

        if (rb_pop(&pipe->buf, &c))
        {
            buf[read_count++] = c;
            
            if (pipe->writer_pid != -1)
            {
                sched_wake_pid(pipe->writer_pid);
                pipe->writer_pid = -1;
            }

            sti();
            continue;
        }

        if (pipe->flags & WRITE_OPEN)
        {
            // hey, the write_end is still ON
            // so the RingBuf is just temporarily empty.
            // Let's wait for more input

            Task *curr_tsk = get_curr_task();
            if (curr_tsk == NULL)
            {
                kprint("ALERT: no task running\n");
                sti();
                return read_count;
            }
            pipe->reader_pid = curr_tsk->pid;
            sched_block();
            sti();
        }
        else
        {
            // the write_end is officially closed, we're done reading
            sti();
            return read_count;
        }
    }

    return read_count;
}

uint64_t pipe_write(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buf)
{
    Pipe *pipe = (Pipe *)node->device_data;
    uint64_t write_count = 0;

    /*
    We push each character into the RingBuf.
    If in the middle of the way, we have read_end being closed, return immediately.
    */

    while (write_count < size)
    {
        if (pipe->flags & READ_OPEN)
        {
            cli();
            while (rb_is_full(&pipe->buf))
            {
                if (!(pipe->flags & READ_OPEN))
                {
                    // hey, the read_end is closed already, no need to write anymore
                    sti();
                    return write_count;
                }

                Task *curr_tsk = get_curr_task();
                if (curr_tsk == NULL)
                {
                    sti();
                    return write_count;
                }
                pipe->writer_pid = curr_tsk->pid;
                sched_block();
            }

            // RingBuf has space, and read_end is still ON here
            rb_push(&pipe->buf, buf[write_count++]);
            if (pipe->reader_pid != -1)
            {
                sched_wake_pid(pipe->reader_pid);
                pipe->reader_pid = -1;
            }
            sti();
        }
        else 
        {
            // read_end is closed
            return write_count;
        }
    }

    return write_count;
}

uint64_t pipe_close_reader(vfs_node_t *node)
{
    Pipe *pipe = (Pipe *)node->device_data;
    pipe->flags &= ~READ_OPEN;

    if (pipe->writer_pid != -1)
    {
        sched_wake_pid(pipe->writer_pid);
        pipe->writer_pid = -1;
    }

    return 0;
}   

uint64_t pipe_close_writer(vfs_node_t *node)
{
    Pipe *pipe = (Pipe *)node->device_data;
    pipe->flags &= ~WRITE_OPEN;

    if (pipe->reader_pid != -1)
    {
        sched_wake_pid(pipe->reader_pid);
        pipe->reader_pid = -1;
    }

    return 0;
}   

vfs_fs_ops_t pipe_read_ops = {
    .read = pipe_read,
    .write = NULL,
    .close = pipe_close_reader,
};

vfs_fs_ops_t pipe_write_ops = {
    .read = NULL,
    .write = pipe_write,
    .close = pipe_close_writer,
};