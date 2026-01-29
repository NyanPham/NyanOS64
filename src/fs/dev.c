#include "dev.h"
#include "vfs.h"
#include "mem/kmalloc.h"
#include "../string.h"
#include "drivers/video.h"
#include "drivers/keyboard.h"
#include "sched/sched.h"
#include "../io.h"
#include "kern_defs.h"
#include "utils/asm_instrs.h"

static vfs_node_t *g_stdin_node = NULL;
static vfs_node_t *g_stdout_node = NULL;

static vfs_node_t *g_dev_list = NULL;

/**
 * @brief Writes to the screen
 */
uint64_t stdout_write(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buf)
{
    // video_write can print a null-terminated string
    // here, we're never sure if the buf to print is null-terminated as we depend on size
    // we split the buf in chunks of 126 bytes (with null termination), and print the chunk each time

    char tmp[128];

    // prints each chunk of 127 bytes
    for (uint64_t i = 0; i < size; i += 126)
    {
        uint64_t nbytes = size - i > 126 ? 126 : size - i;

        memcpy(tmp, &buf[i], nbytes);
        tmp[nbytes] = 0;

        video_write(tmp, White);
    }

    return size;
}

/**
 * @brief Reads nothing
 */
uint64_t stdout_read(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buf)
{
    return 0;
}

/**
 * @brief Reads from the keyboard
 */
uint64_t stdin_read(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buf)
{
    if (size == 0)
    {
        return 0;
    }

    for (uint64_t i = 0; i < size; i++)
    {
        char c = 0;

        // the loop to wait for the keyboard
        while (1)
        {
            // clear interrupts to avoid race condition when keyboard fires signals while we're checking
            cli();

            c = keyboard_get_char();

            // != 0? -> we have key pressed, exit out of the loop
            if (c != 0)
            {
                sti();
                break;
            }

            // otherwise, we wait for the keyboard
            int64_t pid = get_curr_task_pid();
            if (pid != -1)
            {
                keyboard_set_waiting(pid);
            }

            sti();
            sched_block();
        }

        buf[i] = c;

        if (c == '\n')
        {
            return i + 1;
        }
    }

    return size;
}

/**
 * @brief Writes nothing
 */
uint64_t stdin_write(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    return 0;
}

static vfs_fs_ops_t stdout_ops =
    {
        .read = stdout_read,
        .write = stdout_write,
        .open = NULL,
        .close = NULL,
        .finddir = NULL,
        .create = NULL,
};

static vfs_fs_ops_t stdin_ops =
    {
        .read = stdin_read,
        .write = stdin_write,
        .open = NULL,
        .close = NULL,
        .finddir = NULL,
        .create = NULL,
};

/**
 * @brief Inits stdio
 */
void dev_init_stdio()
{
    g_stdin_node = kmalloc(sizeof(vfs_node_t));
    strcpy(g_stdin_node->name, "stdin");
    g_stdin_node->flags = VFS_CHAR_DEVICE;
    g_stdin_node->length = 0;
    g_stdin_node->ops = &stdin_ops;

    g_stdout_node = kmalloc(sizeof(vfs_node_t));
    strcpy(g_stdout_node->name, "stdout");
    g_stdout_node->flags = VFS_CHAR_DEVICE;
    g_stdout_node->length = 0;
    g_stdout_node->ops = &stdout_ops;
}

void dev_attach_stdio(file_handle_t **fd_tbl)
{
    if (g_stdin_node == NULL || g_stdout_node == NULL)
    {
        return;
    }

    // FD 0: stdin
    file_handle_t *h_in = kmalloc(sizeof(file_handle_t));
    h_in->node = g_stdin_node;
    h_in->offset = 0;
    h_in->mode = 1; // read mode
    h_in->ref_count = 1;
    fd_tbl[0] = h_in;

    // FD 1: stdout
    file_handle_t *h_out = kmalloc(sizeof(file_handle_t));
    h_out->node = g_stdout_node;
    h_out->offset = 0;
    h_out->mode = 2; // write mode
    h_out->ref_count = 1;
    fd_tbl[1] = h_out;

    // FD 2: stderr (temporary shared with stdout :3 )
    file_handle_t *h_err = kmalloc(sizeof(file_handle_t));
    h_err->node = g_stdout_node;
    h_err->offset = 0;
    h_err->mode = 2;
    h_err->ref_count = 1;
    fd_tbl[2] = h_err;
}

int dev_register(vfs_node_t *node)
{
    if (node == NULL)
    {
        return -1;
    }
    node->next = g_dev_list;
    g_dev_list = node;

    return 0;
}

vfs_node_t *dev_find(const char *name)
{
    vfs_node_t *curr_dev = g_dev_list;
    while (curr_dev != NULL)
    {
        if (strcmp(curr_dev->name, name) == 0)
        {
            return curr_dev;
        }
        curr_dev = curr_dev->next;
    }

    return NULL;
}

void dev_init()
{
}