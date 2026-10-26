#ifndef SYSCALL_ARGS_H
#define SYSCALL_ARGS_H

#include <stdint.h>
#define WIN_PARAMS_TITLE_SIZE 256

#define WIN_MOVABLE (1 << 0)
#define WIN_RESIZABLE (1 << 1)
#define WIN_MINIMIZABLE (1 << 2)

typedef struct WinParams
{
    int64_t x;
    int64_t y;
    uint64_t width;
    uint64_t height;
    char title[WIN_PARAMS_TITLE_SIZE];
    uint32_t flags;
} WinParams_t;

typedef struct ProcInfo
{
    int pid;
    int ppid;
    int state;
    uint64_t heap_kb;
    uint8_t has_win;
    char name[64];
    char cwd[64];
} ProcInfo_t;

typedef struct MemInfo
{
    uint64_t total_pages;
    uint64_t used_pages;
    uint64_t free_pages;
    uint64_t total_kb;
    uint64_t used_kb;
    uint64_t free_kb;
} MemInfo_t;

#endif