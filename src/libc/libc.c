#include "libc.h"

static inline uint64_t syscall(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    uint64_t ret;
    register uint64_t rax asm("rax") = sys_num;
    register uint64_t rdi asm("rdi") = arg1;
    register uint64_t rsi asm("rsi") = arg2;
    register uint64_t r14 asm("r14") = arg3;

    /*
    Input:
        rax: sys_num
        rsi: arg1
        rdi: arg2
        rdx: arg3
    
    Output:
        rax: ret
    */
    asm volatile (
        "syscall"
        : "=a"(ret)
        : "r"(rax),
          "r"(rdi),
          "r"(rsi),
          "r"(r14)
        : "rcx", "r11", "memory"
    );

    return ret;
}

void exit(int status)
{
    syscall(8, (uint64_t)status, 0, 0);
    while (1) {};
}

void print(const char* str, uint32_t color)
{
    syscall(1, (uint64_t)str, (uint64_t)color, 0);
}

int open(const char* pathname, uint32_t flags)
{
    // syscall 10: sys_open
    return (int)syscall(10, (uint64_t)pathname, (uint64_t)flags, 0);
}

int close(int fd)
{
    // syscall 11: sys_close
    return (int)syscall(11, (uint64_t)fd, 0, 0);
}

int read(int fd, void* buf, uint64_t count)
{
    // syscall 12: sys_read
    return (int)syscall(12, (uint64_t)fd, (uint64_t)buf, count);
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len]) 
    {
        len++;
    }
    return len;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    while (n > 0 && *s1 && (*s1 == *s2)) 
    {
        s1++; 
        s2++;
        n--;
    }
    if (n == 0)
    {
        return 0;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void* memset(void *s, int c, size_t n)
{
    unsigned char* p = s;
    while(n--) 
    {
        *p++ = (unsigned char)c;
    }
    return s;
}