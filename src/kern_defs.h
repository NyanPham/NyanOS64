#ifndef KERN_DEFS_H
#define KERN_DEFS_H

#define USER_STACK_TOP 0x1000000
#define PAGE_SIZE 4096
#define KERN_BASE 0xFFFFFFFF80000000
#define KERN_HEAP_START 0xFFFFFFFF90000000
#define USER_HEAP_START 0x20000000

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
} GBA_Color;

#endif