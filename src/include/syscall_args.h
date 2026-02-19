#ifndef SYSCALL_ARGS_H
#define SYSCALL_ARGS_H

#include <stdint.h>
#define WIN_PARAMS_TITLE_SIZE 256

#define WIN_MOVABLE (1 << 0)
#define WIN_RESIZABLE (1 << 1)
#define WIN_MINIMIZABLE (1 << 2)

#define WIN_TITLE_BAR_H 0x14

typedef struct WinParams
{
    int64_t x;
    int64_t y;
    uint64_t width;
    uint64_t height;
    const char title[WIN_PARAMS_TITLE_SIZE];
    uint32_t flags;
} WinParams_t;

#endif