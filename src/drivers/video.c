#include "video.h"
#include "font.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "mem/kmalloc.h"
#include "kern_defs.h"
#include "../string.h"
#include "drivers/serial.h" // debugging
#include "gui/window.h"

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
static uint64_t g_pitch32 = 0;
static uint64_t g_rows_on_screen = 0;
static uint64_t g_visible_rows = 0;

static uint32_t *g_back_buf = NULL;

typedef enum
{
    ANSI_NORMAL, // normal text
    ANSI_ESC,    // Escape (\033)
    ANSI_CSI,    // '[' after ESC
} ansi_state_t;

static ansi_state_t g_ansi_state = ANSI_NORMAL;
static char g_ansi_buf[32]; // buff to save nums like "10 20"
static int g_ansi_idx = 0;
static uint32_t g_curr_color = White;

static uint32_t g_ansi_palette[] =
    {
        Black,   // 30
        Red,     // 31
        Green,   // 32
        Yellow,  // 33
        Blue,    // 34
        Magenta, // 35
        Cyan,    // 36
        White,   // 37
};

typedef struct
{
    char glyph;
    uint32_t color;
} TermCell;

static TermCell *buf = (TermCell *)BUF_VIRT_ENTRY;

void video_refresh(void);

void video_init_buf()
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
    
    g_back_buf = (uint32_t *)kmalloc(g_pitch32 * g_fb_height * sizeof(uint32_t));

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
    uint32_t *screen_ptr = g_fb_ptr + (y * 8 * g_pitch32) + (x * 8);

    for (int gy = 0; gy < 8; gy++)
    {
        for (int gx = 0; gx < 8; gx++)
        {
            screen_ptr[gx] = (glyph[gy] >> gx) & 1 ? color : Black;
        }
        screen_ptr += g_pitch32;
    }
}

static int my_atoi(const char *s)
{
    int res = 0;

    while (*s >= '0' && *s <= '9')
    {
        res = res * 10 + (*s - '0');
        s++;
    }

    return res;
}

static void execute_ansi_command(char cmd)
{
    // analyze the g_ansi_buf
    int params[4] = {0};
    int param_cnt = 0;
    char *ptr = g_ansi_buf;
    char *start = ptr;

    // split by ';'
    while (*ptr)
    {
        if (*ptr == ';')
        {
            *ptr = 0;
            if (param_cnt < 4)
            {
                params[param_cnt++] = my_atoi(start);
            }
            start = ptr + 1;
        }
        ptr++;
    }

    if (param_cnt < 4)
    {
        params[param_cnt++] = my_atoi(start);
    }

    // now process the cmd
    switch (cmd)
    {
    case 'H': // cmd ESC [ y; x H -> move the cursor
    case 'f':
    {
        int r = (params[0] > 0) ? params[0] : 1;
        int c = (params[1] > 0) ? params[1] : 1;
        g_cursor_y = r - 1; // ANSI index starts at 1, NyanOS's is at 0
        g_cursor_x = c - 1;
        if (g_cursor_x >= BUF_COL)
        {
            g_cursor_x = BUF_COL - 1;
        }
        break;
    }
    case 'J': // ESC [ 2 J -> clear the screen
    {
        if (params[0] == 2)
        {
            video_clear();
        }
        break;
    }
    case 'm': // ESC [31 m -> change color
    {
        for (int i = 0; i < param_cnt; i++)
        {
            int code = params[i];
            if (code >= 30 && code <= 37)
            {
                g_curr_color = g_ansi_palette[code - 30];
            }
            else if (code == 0)
            {
                g_curr_color = White;
            }
        }
        break;
    }
    case 'l': // reset mode
    case 'h': // set mode
    {
        // trivial
        // TODO:
        break;
    }
    default:
    {
        char tmp[2];
        tmp[0] = cmd;
        tmp[1] = 0;
        kprint("Unknown ansi command: ");
        kprint(tmp);
        kprint("\n");
    }
    }
}

/**
 * @brief Process one char with state machine
 */
static void video_putc_internal(char c)
{
    // Round 1: we're nor-malle
    if (g_ansi_state == ANSI_NORMAL)
    {
        if (c == '\033')
        {
            g_ansi_state = ANSI_ESC;
        }
        else
        {
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
                    buf[idx].color = g_curr_color;
                }
            }
            else
            {
                size_t idx = g_cursor_y * BUF_COL + g_cursor_x;
                if (idx < BUF_ROW * BUF_COL)
                {
                    buf[idx].glyph = c;
                    buf[idx].color = g_curr_color;
                }
                g_cursor_x++;

                if (g_cursor_x >= BUF_COL)
                {
                    g_cursor_x = 0;
                    g_cursor_y++;
                }
            }
        }
    }
    // Round 2: let's run away from here
    else if (g_ansi_state == ANSI_ESC)
    {
        if (c == '[')
        {
            g_ansi_state = ANSI_CSI;
            g_ansi_idx = 0;
            memset(g_ansi_buf, 0, sizeof(g_ansi_buf));
        }
        else
        {
            g_ansi_state = ANSI_NORMAL;
        }
    }
    // Round 3: record those dance
    else if (g_ansi_state == ANSI_CSI)
    {
        if ((c >= '0' && c <= '9') || c == ';' || c == '?')
        {
            // 10;20?
            if (g_ansi_idx < sizeof(g_ansi_buf) - 1)
            {
                g_ansi_buf[g_ansi_idx] = c;
                g_ansi_idx++;
            }
        }
        else
        {
            // H, J, m, ...?
            execute_ansi_command(c);
            g_ansi_state = ANSI_NORMAL;
        }
    }
}

void video_scroll(int delta)
{
    int64_t new_scroll = (int64_t)g_scroll_y + delta;

    if (new_scroll < 0)
    {
        new_scroll = 0;
    }

    int64_t max_scroll = (int64_t)g_cursor_y - g_visible_rows + 1;
    if (max_scroll < 0)
    {
        max_scroll = 0;
    }

    if (new_scroll > max_scroll)
    {
        new_scroll = max_scroll;
    }

    g_scroll_y = (uint64_t)new_scroll;
    video_refresh();
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
            draw_char_at(x, y, c, color);
        }
    }

    video_draw_overlay();
    window_paint();
}

void video_clear()
{
    memset(g_back_buf, 0, g_pitch32 * g_fb_height * sizeof(uint32_t));
    memset(buf, 0, 75 * PAGE_SIZE);

    g_cursor_x = 0;
    g_cursor_y = 0;
    g_scroll_y = 0;

    g_ansi_state = ANSI_NORMAL;
    g_curr_color = White;
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

void video_draw_overlay()
{
    uint64_t h = g_fb_height;

    for (int y = h - 100; y < h - 50; y++)
    {
        for (int x = 16; x < 66; x++)
        {
            put_pixel(x, y, White);
        }
    }

    for (int y = h - 85; y < h - 65; y++)
    {
        for (int x = 31; x < 51; x++)
        {
            put_pixel(x, y, Red);
        }
    }
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