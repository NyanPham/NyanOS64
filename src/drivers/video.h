#ifndef VIDEO_H
#define VIDEO_H

#include <limine.h>
#include <stdint.h>

void video_init(struct limine_framebuffer* fb);
void video_write(const char* str, uint32_t color);
void video_clear();
void video_scroll(int delta);

#endif