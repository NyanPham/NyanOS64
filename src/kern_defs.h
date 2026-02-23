#ifndef KERN_DEFS_H
#define KERN_DEFS_H

#include <stdint.h>
#include "include/color.h"

#define USER_STACK_TOP 0x1000000
#define PAGE_SIZE 0x1000
#define KERN_BASE 0xFFFFFFFF80000000
#define KERN_HEAP_START 0xFFFFFFFF90000000
#define USER_HEAP_START 0x20000000
#define USER_MMAP_START 0x40000000
#define USER_MMAP_SIZE 0x40000000
#define USER_STACK_PAGES 0x2

#define O_NONBLOCK 0x1

#define CHAR_W 8 // based on the font.h
#define CHAR_H 8

typedef struct TermCell
{
    char glyph;
    GBA_Color color;
} TermCell;

typedef struct Pixel
{
    uint32_t color;
} Pixel;

typedef struct Rect
{
    int64_t x;
    int64_t y;
    uint64_t w;
    uint64_t h;
    struct Rect *next;
} Rect;

#endif