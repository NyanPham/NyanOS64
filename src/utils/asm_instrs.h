#ifndef ASM_INSTRS_H
#define ASM_INSTRS_H

static inline void cli(void)
{
    asm volatile("cli");
}

static inline void sti(void)
{
    asm volatile("sti");
}

#endif