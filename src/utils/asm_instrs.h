#ifndef ASM_INSTRS_H
#define ASM_INSTRS_H

#include <stdint.h>

static inline void cli(void)
{
    asm volatile("cli");
}

static inline void sti(void)
{
    asm volatile("sti");
}

static inline void hlt(void)
{
    asm volatile("hlt");
}

static inline void hcf(void)
{
    for (;;)
    {
        hlt();
    }
}

static inline void pause(void)
{
    asm volatile("pause");
}

static inline uint64_t read_rflags(void)
{
    uint64_t rflags;
    __asm__ volatile ("pushfq; pop %0" : "=rm" (rflags) :: "memory");
    return rflags;
}

#endif