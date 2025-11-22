#ifndef VIDEO_H
#define VIDEO_H

#include <limine.h>
#include <stdint.h>

void video_init(struct limine_framebuffer* fb);
static void put_pixel(int64_t x, int64_t y, uint32_t color);
void video_put_char(char c, uint32_t color);
void video_write(const char* str, uint32_t color);
void video_clear();
void video_scroll(int delta);

#endif