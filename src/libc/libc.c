#include "libc.h"

static inline uint64_t syscall(uint64_t sys_num, uint64_t arg1, uint64_t arg2)
{
    uint64_t ret;

    /*
    Input:
        rax: sys_num
        rsi: arg1
        rdi: arg2
    
    Output:
        rax: ret
    */
    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(sys_num),
          "S"(arg1),
          "d"(arg2)
        : "rcx", "r11", "memory"
    );

    return ret;
}

void exit(int status)
{
    syscall(8, (uint64_t)status, 0);
    while (1) {};
}

void print(const char* str, uint32_t color)
{
    syscall(1, (uint64_t)str, (uint64_t)color);
}