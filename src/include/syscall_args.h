#ifndef SYSCALL_ARGS_H
#define SYSCALL_ARGS_H

#include <stdint.h>

typedef struct WinParams
{
    int64_t x;
    int64_t y;
    uint64_t width;
    uint64_t height;
    const char title[256];
} WinParams_t;

#endif