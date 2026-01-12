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
    // Data buff and sync
    RingBuf buf;
    int64_t waiting_pid;

    // States (for hotkeys)
    bool ctrl_pressed;
    bool alt_pressed;
    bool shift_pressed;
    bool caps_lock;

    uint8_t last_scancode;
} KeyboardDevice;

static volatile uint8_t scancode = 0;
static volatile KeyboardDevice kbd_dev;

#define PS2_DATA_PORT 0x60
#define KBD_TBL_SIZE 0x80 // 128
#define BREAK_CODE 0x80
#define KEY_PAGE_UP 0x49
#define KEY_PAGE_DOWN 0x51
#define KEY_ALT_PRESSED 0x38
#define KEY_ALT_RESLEASED 0xB8
#define KEY_CTRL_PRESSED 0x1D
#define KEY_CTRL_RESLEASED 0x9D
#define KEY_LSHIFT_PRESSED  0x2A
#define KEY_LSHIFT_RELEASED 0xAA
#define KEY_RSHIFT_PRESSED  0x36
#define KEY_RSHIFT_RELEASED 0xB6
#define KEY_CAPSLOCK_PRESSED 0x3A
#define KEY_UP 0x48
#define KEY_DOWN 0x50

// Scancode -> ASCII
char kbd_tbl[KBD_TBL_SIZE] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0,   '*',
    0,  ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

char kbd_tbl_shift[KBD_TBL_SIZE] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',   0,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',   0,   '*',
    0,  ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/**
 * @brief Sends ansi sequence
 * Stores the whole sequence to the keyboard buffer
 * Fires only one key pressed event, with key being
 * the first character of the sequence.
 */
static void send_ansi_sequence(const char *seq)
{
    while (*seq)
    {
        char c = *seq;
        Event e = {
            .type = EVENT_KEY_PRESSED,
            .key = c,
        };

        rb_push(&kbd_dev.buf, c);
        seq++;
        event_queue_push(&g_event_queue, e);
    }
}

static void keyboard_handler(void *regs)
{
    (void)regs;
    scancode = inb(PS2_DATA_PORT);

    // Update state modifiers
    switch (scancode)
    {
    // ctrl
    case KEY_CTRL_PRESSED:
        kbd_dev.ctrl_pressed = true;
        break;
    case KEY_CTRL_RESLEASED:
        kbd_dev.ctrl_pressed = false;
        break;
    
    // alt
    case KEY_ALT_PRESSED:
        kbd_dev.alt_pressed = true;
        break;
    case KEY_ALT_RESLEASED:
        kbd_dev.alt_pressed = false;
        break;
    
    // shift
    case KEY_LSHIFT_PRESSED:
    case KEY_RSHIFT_PRESSED:
        kbd_dev.shift_pressed = true;
        break;
    case KEY_LSHIFT_RELEASED:
    case KEY_RSHIFT_RELEASED:
        kbd_dev.shift_pressed = false;
        break;
    
    // capslock
    case KEY_CAPSLOCK_PRESSED:
        kbd_dev.caps_lock = !kbd_dev.caps_lock;
        break;
    
    default:
        // fall through, does nothing :/
    }

    if (scancode & BREAK_CODE)
    {
        // Is break code (release the button), ignore it
        return;
    }

    if (scancode < KBD_TBL_SIZE)
    {
        if (scancode == KEY_PAGE_UP)
        {
            // send ESC[5~
            send_ansi_sequence("\033[5~");
            return;
        }

        if (scancode == KEY_PAGE_DOWN)
        {
            // send ESC[6~
            send_ansi_sequence("\033[6~");
            return;
        }

        if (scancode == KEY_UP)
        {
            // send ESC[A
            send_ansi_sequence("\033[A");
            return;
        }
        if (scancode == KEY_DOWN)
        {
            // send ESC[B
            send_ansi_sequence("\033[B");
            return;
        }

        char ascii_char = kbd_dev.shift_pressed
                              ? kbd_tbl_shift[scancode]
                              : kbd_tbl[scancode];

        if (kbd_dev.caps_lock &&
            ((ascii_char >= 'a' && ascii_char <= 'z') ||
             (ascii_char >= 'A' && ascii_char <= 'Z')))
        {
            ascii_char ^= 0x20;
        }

        if (ascii_char != 0)
        {
            Event e;
            e.type = EVENT_KEY_PRESSED;
            e.key = ascii_char;
            e.modifiers = 0;
            if (kbd_dev.ctrl_pressed)
            {
                e.modifiers |= MOD_CTRL;
            }
            if (kbd_dev.alt_pressed)
            {
                e.modifiers |= MOD_ALT;
            }
            if (kbd_dev.shift_pressed)
            {
                e.modifiers |= MOD_SHIFT;
            }

            rb_push(&kbd_dev.buf, ascii_char);

            event_queue_push(&g_event_queue, e);
            // if (kbd_dev.waiting_pid != -1)
            // {
            //     sched_wake_pid(kbd_dev.waiting_pid);
            //     kbd_dev.waiting_pid = -1;
            // }
        }
    }
}

void keyboard_set_waiting(int64_t pid)
{
    kbd_dev.waiting_pid = pid;
}

char keyboard_get_char()
{
    char c;
    if (rb_pop(&kbd_dev.buf, &c))
    {
        return c;
    }
    return 0;
}

void keyboard_init(void)
{
    rb_init(&kbd_dev.buf);
    kbd_dev.waiting_pid = -1;
    register_irq_handler(1, keyboard_handler);
    // pic_clear_mask(1);
}

bool keyboard_ctrl_presssed(void)
{
    return kbd_dev.ctrl_pressed;
}

bool keyboard_alt_presssed(void)
{
    return kbd_dev.alt_pressed;
}

bool keyboard_shift_presssed(void)
{
    return kbd_dev.shift_pressed;
}

bool keyboard_is_caps_lock(void)
{
    return kbd_dev.caps_lock;
}
