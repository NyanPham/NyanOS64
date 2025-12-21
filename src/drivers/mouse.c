#include "mouse.h"
#include "../io.h"
#include "../arch/irq.h"
#include "video.h"
#include "serial.h"

#define MOUSE_PORT_DATA 0x60
#define MOUSE_PORT_CMD 0x64
#define MOUSE_IRQ_VECTOR 0x2C // 32 (exception) + 12 (IRQ) = 44 (0x2C)

static uint8_t mouse_cycle = 0; // (0: stat , 1: x, 2: y)
static int8_t mouse_byte[3];
static int64_t g_mouse_x = 100;
static int64_t g_mouse_y = 100;

#define CURSOR_W 4
#define CURSOR_H 4

static uint32_t mouse_bg[CURSOR_W * CURSOR_H]; // mouse background buffer to do: Save -> draw -> restore

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

static void save_mouse_bg()
{
    for (int y = 0; y < CURSOR_H; y++)
    {
        for (int x = 0; x < CURSOR_W; x++)
        {
            mouse_bg[y * CURSOR_W + x] = video_get_pixel(g_mouse_x + x, g_mouse_y + y);
        }
    }
}

static void draw_mouse(uint32_t color)
{
    for (int y = 0; y < CURSOR_H; y++)
    {
        for (int x = 0; x < CURSOR_W; x++)
        {
            video_plot_pixel(g_mouse_x + x, g_mouse_y + y, color);
        }
    }
}

static void restore_mouse_bg()
{
    for (int y = 0; y < CURSOR_H; y++)
    {
        for (int x = 0; x < CURSOR_W; x++)
        {
            video_plot_pixel(g_mouse_x + x, g_mouse_y + y, mouse_bg[y * CURSOR_W + x]);
        }
    }
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

    mouse_byte[mouse_cycle++] = data;
    if (mouse_cycle >= 3)
    {
        mouse_cycle = 0;

        // Note
        // byte 0: stat (left/right btn, sign bit, etc)
        // byte 1: delta x
        // byte 2: delta y

        // bit 3 must be always 1
        if ((mouse_byte[0] & 0x08) == 0)
        {
            return;
        }

        int8_t dx = (int8_t)mouse_byte[1];
        int8_t dy = (int8_t)mouse_byte[2];

        // RESTORE the background
        restore_mouse_bg();

        // update the latest mouse coord
        g_mouse_x += dx;
        g_mouse_y -= dy; // mouse's Y logic is opposite than ours

        if (g_mouse_x < 0)
            g_mouse_x = 0;

        if (g_mouse_y < 0)
            g_mouse_y = 0;

        if (g_mouse_x >= (int64_t)video_get_width() - CURSOR_W)
            g_mouse_x = video_get_width() - CURSOR_W;

        if (g_mouse_y >= (int64_t)video_get_height() - CURSOR_H)
            g_mouse_y = video_get_height() - CURSOR_H;

        // SAVE the background
        save_mouse_bg();

        // draw the mouse again
        draw_mouse(0xFF0000);

        if (mouse_byte[0] & 0x01)
        {
            draw_mouse(0x00FF00);
            save_mouse_bg();
        }

        // TEST:
        if (mouse_byte[0] & 0x01)
        {
            kprint("Left click!\n");
        }
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

    save_mouse_bg();

    // finally, hook it up to our IRQ system
    register_irq_handler(12, mouse_handler);

    kprint("Mouse inited!\n");
}

int64_t mouse_get_x(void)
{
    return g_mouse_x;
}

int64_t mouse_get_y(void)
{
    return g_mouse_y;
}
