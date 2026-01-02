#include "mouse.h"
#include "../io.h"
#include "arch/irq.h"
#include "drivers/video.h"
#include "kern_defs.h"
#include "serial.h"

#define MOUSE_PORT_DATA 0x60
#define MOUSE_PORT_CMD 0x64
#define MOUSE_IRQ_VECTOR 0x2C // 32 (exception) + 12 (IRQ) = 44 (0x2C)

#define CURSOR_W 4
#define CURSOR_H 4

static Mouse mouse =
    {
        .cycle = 0,
        .byte = {0, 0, 0},
        .x = 100,
        .y = 100,
        .w = CURSOR_W,
        .h = CURSOR_H,
};

static void mouse_wait(uint8_t type)
{
    uint32_t timeout = 100000;
    if (type == 0) // waiting to read
    {
        while (timeout--)
        {
            if ((inb(MOUSE_PORT_CMD) & 1) == 1)
            {
                return;
            }
        }
    }
    else // waiting to write
    {
        while (timeout--)
        {
            if ((inb(MOUSE_PORT_CMD) & 2) == 0)
            {
                return;
            }
        }
    }
}

static void mouse_write(uint8_t write)
{
    mouse_wait(1);
    outb(MOUSE_PORT_CMD, 0xD4);
    mouse_wait(1);
    outb(MOUSE_PORT_DATA, write);
}

static uint8_t mouse_read()
{
    mouse_wait(0);
    return inb(MOUSE_PORT_DATA);
}

/**
 * @brief Handle the interrupts from ISR
 */
static void mouse_handler(void *regs)
{
    (void)regs;

    uint8_t stat = inb(MOUSE_PORT_CMD);

    // check if the 5th bit (auxiliary mouse dev flag) exists
    // or else, it's some other controllers
    if (!(stat & 0x20))
    {
        return;
    }

    uint8_t data = inb(MOUSE_PORT_DATA);

    mouse.byte[mouse.cycle++] = data;

    if (mouse.cycle >= 3)
    {
        mouse.cycle = 0;

        // Note
        // byte 0: stat (left/right btn, sign bit, etc)
        // byte 1: delta x
        // byte 2: delta y

        // bit 3 must be always 1
        if ((mouse.byte[0] & 0x08) == 0)
        {
            kprint("Error: Bag alignment!\n");
            return;
        }

        /*
        NOTE: Delta X and Delta Y sent from the mouse
        are 9-bits. The byte 1 and 2 are the 8-bit quantity
        values of the delta, and the sign bit is in the byte 0.
        byte[0] bit 5 is sign-bit for delta y
        byte[0] bit 4 is sign-bit for delta x
        So, we treat byte 1 and 2 as raw unsigned values, and or
        with negative leading bits if the corresponding
        flag of sign bit is set.
        */

        int16_t dx = (uint8_t)mouse.byte[1];
        int16_t dy = (uint8_t)mouse.byte[2];

        if (mouse.byte[0] & 0x10)
        {
            dx |= 0xFF00;
        }

        if (mouse.byte[0] & 0x20)
        {
            dy |= 0xFF00;
        }

        // update the latest mouse coord
        mouse.x += dx;
        mouse.y -= dy; // mouse's Y logic is opposite than ours

        if (mouse.x < 0)
            mouse.x = 0;

        if (mouse.y < 0)
            mouse.y = 0;

        if (mouse.x >= (int64_t)video_get_width() - CURSOR_W)
            mouse.x = video_get_width() - CURSOR_W;

        if (mouse.y >= (int64_t)video_get_height() - CURSOR_H)
            mouse.y = video_get_height() - CURSOR_H;
    }
}

void mouse_init(void)
{
    uint8_t stat;

    // enable auxiliary device mouse
    mouse_wait(1);
    outb(MOUSE_PORT_CMD, 0xA8);

    // enable IRQ 12
    mouse_wait(1);
    outb(MOUSE_PORT_CMD, 0x20);
    mouse_wait(0);
    stat = (inb(MOUSE_PORT_DATA) | 2); // turn the bit 2 on

    mouse_wait(1);
    outb(MOUSE_PORT_CMD, 0x60);
    mouse_wait(1);
    outb(MOUSE_PORT_DATA, stat); // update the stat

    // default settings for the mouse
    mouse_write(0xF6);
    mouse_read();

    // start data reporting
    mouse_write(0xF4);
    mouse_read();

    // finally, hook it up to our IRQ system
    register_irq_handler(12, mouse_handler);

    kprint("Mouse inited!\n");
}

int64_t mouse_get_x(void)
{
    return mouse.x;
}

int64_t mouse_get_y(void)
{
    return mouse.y;
}

int64_t mouse_get_stat(void)
{
    return mouse.stat;
}