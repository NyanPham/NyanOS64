#ifndef VIDEO_H
#define VIDEO_H

#include <limine.h>
#include <stdint.h>

void video_init(struct limine_framebuffer* fb);
void video_write(const char* str, uint32_t color);
void video_clear();
void video_scroll(int delta);
void video_plot_pixel(int64_t x, int64_t y, uint32_t color);
uint64_t video_get_width(void);
uint64_t video_get_height(void);

#endif