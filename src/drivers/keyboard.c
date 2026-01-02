#include "keyboard.h"
#include "io.h"
#include "arch/irq.h"
#include "serial.h"
#include "video.h"
#include "utils/ring_buf.h"
#include "event/event.h"
// #include "pic.h"

extern EventBuf g_event_queue;

typedef struct
{
    RingBuf buf;
    int64_t waiting_pid;
} kbd_buf_t;


static volatile uint8_t scancode = 0;
static volatile kbd_buf_t kbd_buf;

#define PS2_DATA_PORT 0x60
#define BREAK_CODE 0x80
#define PAGE_UP_CODE 0x49
#define PAGE_DOWN_CODE 0x51
#define KBD_TBL_SIZE 0x80 // 128

// Scancode -> ASCII
char kbd_tbl[KBD_TBL_SIZE] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0,   '*',
    0,  ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/**
 * @brief Sends ansi sequence
 * Stores the whole sequence to the keyboard buffer
 * Fires only one key pressed event, with key being
 * the first character of the sequence.
 */
static void send_ansi_sequence(const char* seq)
{
    Event e = {
        .type = EMPTY,
        .key = 0,
    };

    if (*seq)
    {
        e.type = EVENT_KEY_PRESSED;
        e.key = *seq;
    }

    while (*seq)
    {
        char c = *seq;
        rb_push(&kbd_buf.buf, c);
        seq++;
    }

    event_queue_push(&g_event_queue, e);
}

static void keyboard_handler(void *regs)
{
    (void)regs;
    scancode = inb(PS2_DATA_PORT);

    if (scancode & BREAK_CODE)
    {
        // Is break code (release the button), ignore it
        return;
    }

    if (scancode < KBD_TBL_SIZE)
    {
        if (scancode == PAGE_UP_CODE)
        {
            // send ESC[5~
            send_ansi_sequence("\033[5~");
            return;
        }

        if (scancode == PAGE_DOWN_CODE)
        {
            // send ESC[6~
            send_ansi_sequence("\033[6~");    
            return;
        }

        char ascii_char = kbd_tbl[scancode];

        if (ascii_char != 0)
        {
            // kprint("KEY: ");
            // serial_write(ascii_char);
            // kprint("\n"); 

            rb_push(&kbd_buf.buf, ascii_char);

            Event e = {
                .type = EVENT_KEY_PRESSED,
                .key = ascii_char,
            };

            event_queue_push(&g_event_queue, e);
            // if (kbd_buf.waiting_pid != -1)
            // {
            //     sched_wake_pid(kbd_buf.waiting_pid);
            //     kbd_buf.waiting_pid = -1;
            // }
        }
    }
}

void keyboard_set_waiting(int64_t pid)
{
    kbd_buf.waiting_pid = pid;
}

char keyboard_get_char()
{
    char c;
    if (rb_pop(&kbd_buf.buf, &c))
    {
        return c;
    }
    return 0;
}

void keyboard_init(void)
{
    rb_init(&kbd_buf.buf);
    kbd_buf.waiting_pid = -1;
    register_irq_handler(1, keyboard_handler);
    // pic_clear_mask(1);
}