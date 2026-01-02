#ifndef SYSCALL_ARGS_H
#define SYSCALL_ARGS_H

#include <stdint.h>
#define WIN_PARAMS_TITLE_SIZE 256

typedef struct WinParams
{
    int64_t x;
    int64_t y;
    uint64_t width;
    uint64_t height;
    const char title[WIN_PARAMS_TITLE_SIZE];
} WinParams_t;

#endif