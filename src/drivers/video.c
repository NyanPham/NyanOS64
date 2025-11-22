#include "video.h"
#include "font.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include <stddef.h>

#define BLACK 0x000000
#define BUF_VIRT_ENTRY 0xFFFFFFFFB0000000
#define BUF_ROW 300
#define BUF_COL 128
#define MARGIN_BOTTOM 0x8

extern uint64_t hhdm_offset;
extern uint64_t* kern_pml4;
extern void* memset(void *s, int c, size_t n);

static struct limine_framebuffer* g_fb = NULL;
static uint64_t g_cursor_x = 0;
static uint64_t g_cursor_y = 0;
static uint64_t g_scroll_y = 0;

typedef struct
{
    char glyph;
    uint32_t color;
} TermCell;

static TermCell* buf = (TermCell*)BUF_VIRT_ENTRY;

void video_init(struct limine_framebuffer* fb)
{
    g_fb = fb;
    g_cursor_x = 0;
    g_cursor_y = 0;

    uint32_t* fb_ptr = (uint32_t*)g_fb->address;
    for (size_t i = 0; i < g_fb->width * g_fb->height; i++)
    {
        fb_ptr[i] = 0x0; // paint black
    }

    video_init_buf();
}

static void put_pixel(int64_t x, int64_t y, uint32_t color)
{
    if (x < 0 || x >= g_fb->width || y < 0 || y >= g_fb->height)
    {
        return;
    }

    // to cal the pos in RAM: (y * pitch) + (x * 4) -> (y * pitch / 4) + x;
    // where pitch the is byte_num of 1 row
    uint32_t* fb_ptr = (uint32_t*)g_fb->address;
    size_t index = y * (g_fb->pitch / 4) + x;
    fb_ptr[index] = color;
}

static void draw_char_at(uint64_t x, uint64_t y, char c, uint32_t color)
{
    uint8_t* glyph = (uint8_t*)font8x8_basic[(int)c];

    for (int gy = 0; gy < 8; gy++)
    {
        for (int gx = 0; gx < 8; gx++)
        {
            uint64_t draw_x = x + gx;
            uint64_t draw_y = y + gy;

            put_pixel(
                draw_x, 
                draw_y, 
                (glyph[gy] >> gx) & 1 ? color : BLACK
            );
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

   uint64_t rows_on_screen = g_fb->height / 8;
   uint64_t visible_rows = rows_on_screen - MARGIN_BOTTOM;
   int64_t max_scroll = (int64_t)g_cursor_y - visible_rows + 1;
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

void video_put_char(char c, uint32_t color)
{
    uint8_t* glyph = (uint8_t*)font8x8_basic[(int)c];

    for (int y = 0; y < 8; y++) // for each row
    {
        for (int x = 0; x < 8; x++) // we check each cell
        {
            // by checking if the pixel is meant to be on?
            put_pixel(
                g_cursor_x + x, 
                g_cursor_y + y, 
                (glyph[y] >> x) & 1 ? color : BLACK
            );
        }
    }

    g_cursor_x += 8;
}

void video_write(const char* str, uint32_t color)
{
    while (*str)
    {
        if (*str == '\n')
        {
            g_cursor_x = 0;
            g_cursor_y++;
        }
        else if (*str == '\b')
        {
            if (g_cursor_x > 0)
            {
                g_cursor_x--;
                size_t idx = g_cursor_y * BUF_COL + g_cursor_x;
                buf[idx].glyph = ' ';
                buf[idx].color = color;
            }
        }
        else 
        {
            size_t idx = g_cursor_y * BUF_COL + g_cursor_x;
            if (idx < BUF_ROW * BUF_COL)
            {
                buf[idx].glyph = *str;
                buf[idx].color = color;
            }
            g_cursor_x++;
        }
        
        if (g_cursor_x >= BUF_COL)
        {
            g_cursor_x = 0;
            g_cursor_y++;
        }

        str++;
    }

    uint64_t rows_on_screen = g_fb->height / 8;
    uint64_t visible_rows = rows_on_screen - MARGIN_BOTTOM;

    if (g_cursor_y >= visible_rows)
    {
        g_scroll_y = g_cursor_y - visible_rows + 1;
    }
    else 
    {
        if (g_scroll_y > g_cursor_y) g_scroll_y = 0;
    }

    video_refresh();
}

void video_refresh()
{
    uint64_t total_rows = g_fb->height / 8;
    uint64_t visible_rows = total_rows - MARGIN_BOTTOM;

    for (uint64_t y = 0; y < total_rows; y++)
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
            uint32_t color = (cell.glyph == 0) ? BLACK : cell.color;
            draw_char_at(x * 8, y * 8, c, color); 
        }
    }
}

void video_clear()
{
    uint32_t* fb_ptr = (uint32_t*)g_fb->address;
    for (size_t i = 0; i < g_fb->width * g_fb->height; i++)
    { 
        fb_ptr[i] = BLACK;
    }

    memset(buf, 0, 75 * PAGE_SIZE);

    g_cursor_x = 0;
    g_cursor_y = 0;
    g_scroll_y = 0;
}

void video_init_buf()
{
    for (size_t i = 0; i < 75; i++)
    {
        void* page_virt_hhdm = pmm_alloc_frame();
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
            VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE
        );
    }
}