#include "keyboard.h"
#include "io.h"
#include "arch/irq.h"
#include "serial.h"
// #include "pic.h"

static volatile uint8_t scancode = 0;
static volatile ring_buf kb_buf;

#define PAGE_UP_CODE 0x49
#define PAGE_DOWN_CODE 0x51

// Scancode -> ASCII
char kbd_tbl[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0,   '*',
    0,  ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static void keyboard_handler(void *regs)
{
    (void)regs;
    scancode = inb(0x60);

    if (scancode & 0x80)
    {
        // Is break code (release the button), ignore it
        return;
    }

    if (scancode < 128)
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

        char ascii = kbd_tbl[scancode];

        if (ascii != 0)
        {
            kprint("KEY: ");
            serial_write(ascii);
            kprint("\n"); 

            kb_buf.buf[kb_buf.head] = ascii;
            kb_buf.head++;
        }
    }
}

char keyboard_get_char()
{
    if (kb_buf.head == kb_buf.tail)
    {
        return 0;
    }

    char c = kb_buf.buf[kb_buf.tail];
    kb_buf.tail++;
    return c;
}

void keyboard_init(void)
{
    for (int i = 0; i < KB_BUF_SIZE; i++)
    {
        kb_buf.buf[i] = 0;
    }
    kb_buf.head = 0;
    kb_buf.tail = 0;
    register_irq_handler(1, keyboard_handler);
    // pic_clear_mask(1);
}