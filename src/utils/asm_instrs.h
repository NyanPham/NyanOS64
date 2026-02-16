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

static inline void hlt(void)
{
    asm volatile("hlt");
}

static void hcf(void)
{
    for (;;)
    {
        hlt();
    }
}

static inline uint64_t get_rflags(void)
{
    uint64_t rflags;
    asm volatile("pushfq; pop %0" : "=r"(rflags));
    return rflags;
}

#endif