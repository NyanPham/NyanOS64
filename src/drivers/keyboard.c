#include "keyboard.h"
#include "io.h"
#include "arch/irq.h"
#include "serial.h"
#include "video.h"
// #include "pic.h"

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
            video_scroll(-1);
            return;
        }

        if (scancode == PAGE_DOWN_CODE)
        {
            video_scroll(1);
            return;
        }

        char ascii_char = kbd_tbl[scancode];

        if (ascii_char != 0)
        {
            // kprint("KEY: ");
            // serial_write(ascii_char);
            // kprint("\n"); 

            kbd_buf.buf[kbd_buf.head] = ascii_char;
            kbd_buf.head++;
            
            sched_wake_pid(1);
        }
    }
}

char keyboard_get_char()
{
    if (kbd_buf.head == kbd_buf.tail)
    {
        return 0;
    }

    char c = kbd_buf.buf[kbd_buf.tail];
    kbd_buf.tail++;
    return c;
}

void keyboard_init(void)
{
    for (int i = 0; i < kbd_buf_SIZE; i++)
    {
        kbd_buf.buf[i] = 0;
    }
    kbd_buf.head = 0;
    kbd_buf.tail = 0;
    register_irq_handler(1, keyboard_handler);
    // pic_clear_mask(1);
}