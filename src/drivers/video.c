#include "video.h"
#include "font.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "mem/kmalloc.h"
#include "kern_defs.h"
#include "../string.h"
#include "drivers/serial.h" // debugging
#include "gui/window.h"
#include "gui/cursor.h"
#include "drivers/mouse.h"
#include "ansi.h"

#include <stddef.h>

#define BUF_VIRT_ENTRY 0xFFFFFFFFB0000000
#define BUF_ROW 300
#define BUF_COL 128
#define MARGIN_BOTTOM 0x8

#define FONT_W 8
#define FONT_H 8

extern uint64_t hhdm_offset;
extern uint64_t *kern_pml4;

static struct limine_framebuffer *g_fb = NULL;
static uint64_t g_cursor_x = 0;
static uint64_t g_cursor_y = 0;
static uint64_t g_scroll_y = 0;

static uint32_t *g_fb_ptr = 0;
static void *g_fb_addr = 0;
static uint64_t g_fb_width = 0;
static uint64_t g_fb_height = 0;
static uint64_t g_pitch32 = 0; // Nums of pixels per row
static uint64_t g_rows_on_screen = 0;
static uint64_t g_visible_rows = 0;

static uint32_t *g_back_buf = NULL;
static volatile bool mouse_moved = false;

static TermCell *buf = (TermCell *)BUF_VIRT_ENTRY;

static AnsiContext g_ansi_ctx;

/* START: ANSI DRIVER IMPLEMENTATION FOR VIDEO */

static void video_driver_put_char(void *data, char c)
{
    (void)data; // we use the static variales, data is not needed

    if (c == '\n')
    {
        g_cursor_x = 0;
        g_cursor_y++;
    }
    else if (c == '\b')
    {
        if (g_cursor_x > 0)
        {
            g_cursor_x--;
            size_t idx = g_cursor_y * BUF_COL + g_cursor_x;
            buf[idx].glyph = ' ';
            buf[idx].color = g_ansi_ctx.color;
        }
    }
    else
    {
        size_t idx = g_cursor_y * BUF_COL + g_cursor_x;
        if (idx < BUF_ROW * BUF_COL)
        {
            buf[idx].glyph = c;
            buf[idx].color = g_ansi_ctx.color;
        }
        g_cursor_x++;

        if (g_cursor_x >= BUF_COL)
        {
            g_cursor_x = 0;
            g_cursor_y++;
        }
    }
}

static void video_driver_set_cursor(void *data, int x, int y)
{
    (void)data;
    g_cursor_x = x;
    g_cursor_y = y;
    if (g_cursor_x >= BUF_COL)
    {
        g_cursor_x = BUF_COL - 1;
    }
    if (g_cursor_y >= BUF_ROW)
    {
        g_cursor_y = BUF_ROW - 1;
    }
}

static void video_driver_set_color(void *data, uint32_t color)
{
    // already done internally in the ansi.c
    (void)data;
    (void)color;
}

static void video_driver_clear(void *data, int mode)
{
    (void)data;
    if (mode == 2)
    {
        video_clear();
    }
}

static const AnsiDriver g_video_driver = {
    .put_char = video_driver_put_char,
    .set_color = video_driver_set_color,
    .set_cursor = video_driver_set_cursor,
    .clear_screen = video_driver_clear,
    .scroll = NULL,
};

/* END: ANSI DRIVER IMPLEMENTATION FOR VIDEO */

void
video_init_buf()
{
    for (size_t i = 0; i < 75; i++)
    {
        void *page_virt_hhdm = pmm_alloc_frame();
        if (page_virt_hhdm == NULL)
        {
            return;
        }
        memset(page_virt_hhdm, 0, PAGE_SIZE);

        uint64_t phys_addr = (uint64_t)page_virt_hhdm - hhdm_offset;
        uint64_t buf_virt_addr = (uint64_t)buf + (i * PAGE_SIZE);
        vmm_map_page(
            kern_pml4,
            buf_virt_addr,
            phys_addr,
            VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE);
    }
}

void video_init(struct limine_framebuffer *fb)
{
    g_fb = fb;
    g_cursor_x = 0;
    g_cursor_y = 0;
    g_fb_addr = g_fb->address;
    g_fb_width = g_fb->width;
    g_fb_height = g_fb->height;
    g_pitch32 = g_fb->pitch / 4;
    g_fb_ptr = (uint32_t *)g_fb_addr;
    g_rows_on_screen = g_fb_height / FONT_H;
    g_visible_rows = g_rows_on_screen - MARGIN_BOTTOM;

    g_back_buf = (uint32_t *)vmm_alloc(g_pitch32 * g_fb_height * sizeof(uint32_t));

    g_ansi_ctx.state = ANSI_NORMAL;
    g_ansi_ctx.color = White;
    g_ansi_ctx.idx = 0;
    memset(g_ansi_ctx.buf, 0, ANSI_BUF_SIZE);

    video_init_buf();
    video_clear();
}

static inline void put_pixel(int64_t x, int64_t y, uint32_t color)
{
    if (x < 0 || x >= (int64_t)g_fb_width || y < 0 || y >= (int64_t)g_fb_height)
    {
        return;
    }

    // instead of writing directly to VRAM of g_fb_ptr, we
    // write to back buffer
    g_back_buf[y * g_pitch32 + x] = color;
}

static void draw_char_at(uint64_t x, uint64_t y, char c, uint32_t color)
{
    uint8_t *glyph = (uint8_t *)font8x8_basic[(int)c];
    uint32_t *screen_ptr = g_back_buf + (y * g_pitch32) + x;

    for (int gy = 0; gy < 8; gy++)
    {
        for (int gx = 0; gx < 8; gx++)
        {
            screen_ptr[gx] = (glyph[gy] >> gx) & 1 ? color : Black;
        }
        screen_ptr += g_pitch32;
    }
}

void video_draw_string(uint64_t x, uint64_t y, const char* str, uint32_t color)
{
    int curr_x = x;
    while (*str)
    {
        if (*str != '\n')
        {
            draw_char_at(curr_x, y, *str, color);
            curr_x += 8;
        }
        str++;
    }
}

/**
 * @brief Process one char with state machine
 */
static void video_putc_internal(char c)
{
    ansi_write_char(&g_ansi_ctx, c, &g_video_driver, NULL);
}

void video_write(const char *str, uint32_t color)
{
    while (*str)
    {
        video_putc_internal(*str);
        str++;
    }

    if (g_cursor_y >= g_visible_rows)
    {
        g_scroll_y = g_cursor_y - g_visible_rows + 1;
    }
    else
    {
        if (g_scroll_y > g_cursor_y)
            g_scroll_y = 0;
    }

    video_refresh();
}

void video_refresh()
{
    for (uint64_t y = 0; y < g_rows_on_screen; y++)
    {
        for (uint64_t x = 0; x < BUF_COL; x++)
        {
            size_t idx = (g_scroll_y + y) * BUF_COL + x;

            if (idx >= BUF_ROW * BUF_COL)
            {
                break;
            }

            TermCell cell = buf[idx];
            char c = (cell.glyph == 0) ? ' ' : cell.glyph;
            uint32_t color = (cell.glyph == 0) ? Black : cell.color;
            draw_char_at(x * 8, y * 8, c, color);
        }
    }

    window_paint();
    draw_mouse();
    video_swap();
}

void video_clear()
{
    memset(g_back_buf, 0, g_pitch32 * g_fb_height * sizeof(uint32_t));
    memset(buf, 0, 75 * PAGE_SIZE);

    g_cursor_x = 0;
    g_cursor_y = 0;
    g_scroll_y = 0;

    g_ansi_ctx.state = ANSI_NORMAL;
    g_ansi_ctx.color = White;
}

/**
 * @brief A wrapper function to call the static inline put_pixel func
 */
void video_plot_pixel(int64_t x, int64_t y, uint32_t color)
{
    put_pixel(x, y, color);
}

uint64_t video_get_width(void)
{
    return g_fb_width;
}

uint64_t video_get_height(void)
{
    return g_fb_height;
}

/**
 * @brief Reads a pixel from screen
 */
uint32_t video_get_pixel(int64_t x, int64_t y)
{
    if (x < 0 || x >= (int64_t)g_fb_width || y < 0 || y >= (int64_t)g_fb_height)
    {
        return 0;
    }

    // instead of reading from the VRAM g_fb_ptr
    // we read from the back buffer
    return g_back_buf[y * g_pitch32 + x];
}

/**
 * @brief Copy all the contents from back buff to VRAM
 * Handles Pitch/Padding correctly to avoid image skewing
 */
void video_swap()
{
    /*
    the VRAM has alignment with pitch32, but back buffer doesn't.
    so we calc the offsets of both separately.

    for back buff, it's simple, we just use the width (in pixels)
    for the VRAM, we based on the pitch, but pitch is in bytes, not pixels,
    so we calc the stride by div it pitch by 4 bytes (== one pixel)
    */
    uint64_t row_size = g_fb_width * sizeof(uint32_t);

    memcpy(g_fb_ptr, g_back_buf, g_fb_height * g_pitch32 * sizeof(uint32_t));
}

void draw_rect(int rect_x, int rect_y, int width, int height, GBA_Color color)
{
    for (int r = rect_x; r < rect_x + width; r++)
    {
        for (int c = rect_y; c < rect_y + height; c++)
        {
            put_pixel(r, c, color);
        }
    }
}