#ifndef VIDEO_H
#define VIDEO_H
#include "kern_defs.h"

#include <limine.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    char glyph;
    uint32_t color;
} TermCell;

typedef struct Pixel
{
    uint32_t color;
} Pixel;

void video_init(struct limine_framebuffer* fb);
void video_write(const char* str, uint32_t color);
void video_clear();
void video_scroll(int delta);
void video_plot_pixel(int64_t x, int64_t y, uint32_t color);
uint64_t video_get_width(void);
uint64_t video_get_height(void);
uint32_t video_get_pixel(int64_t x, int64_t y);
void video_swap(void);
void draw_rect(int rect_x, int rect_y, int width, int height, GBA_Color color);
void video_refresh(void);

bool mouse_ack();
void mouse_set();

#endif