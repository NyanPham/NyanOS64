#ifndef KERN_DEFS_H
#define KERN_DEFS_H

#include <stdint.h>

#define USER_STACK_TOP 0x1000000
#define PAGE_SIZE 0x1000
#define KERN_BASE 0xFFFFFFFF80000000
#define KERN_HEAP_START 0xFFFFFFFF90000000
#define USER_HEAP_START 0x20000000
#define USER_MMAP_START 0x40000000
#define USER_MMAP_SIZE 0x40000000
#define USER_STACK_PAGES 0x2

typedef enum
{
    Black = 0x000000,
    Blue = 0x0000AA,
    Green = 0x00AA00,
    Cyan = 0x00AAAA,
    Red = 0xAA0000,
    Magenta = 0xAA00AA,
    Brown = 0xAA5500,
    White = 0xFFFFFF,
    Yellow = 0xFFFF00,
    Slate = 0x708090,
    Gray = 0x808080,
    LightGray = 0xD3D3D3,
    DarkGray = 0xA9A9A9,
    Orange = 0xFFA500,
    Pink = 0xFFC0CB,
    Purple = 0x800080,
    Teal = 0x008080,
    DarkTeal = 0x006666,
    Navy = 0x000080,
} GBA_Color;

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