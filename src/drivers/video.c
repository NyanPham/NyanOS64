#include "video.h"
#include "font.h"
#include <stddef.h>

#define BLACK 0x000000

static struct limine_framebuffer* g_fb = NULL;
static uint64_t g_cursor_x = 0;
static uint64_t g_cursor_y = 0;

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
            g_cursor_y += 8;
        }
        else if (*str == '\b')
        {
            if (g_cursor_x >= 8)
            {
                g_cursor_x -= 8;
                video_put_char(' ', color);
                g_cursor_x -= 8;
            }
        }
        else 
        {
            video_put_char(*str, color);
        }
        
        if (g_cursor_x >= g_fb->width)
        {
            g_cursor_x = 0;
            g_cursor_y += 8;
        }
        
        str++;
    }
}

void video_clear()
{
    for (uint64_t x = 0; x < g_fb->width; x++)
    {
        for (uint64_t y = 0; y < g_fb->height; y++)
        {
            put_pixel(x, y, BLACK);
        }
    }

    g_cursor_x = 0;
    g_cursor_y = 0;
}