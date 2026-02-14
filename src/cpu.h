#ifndef CPU_H
#define CPU_H

#include <stdint.h>

static inline void cpuid(uint32_t code, uint32_t *eax, uint32_t *ecx, uint32_t *edx, uint32_t *ebx)
{
    asm volatile(
        "cpuid"
        : "=a"(*eax), "=c"(*ecx), "=d"(*edx), "=b"(*ebx)
        : "a"(code));
}

// Read the MSR in 64-bit
static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t low, high;
    asm volatile(
        "rdmsr"
        : "=a"(low), "=d"(high)
        : "c"(msr));

    return ((uint64_t)high << 32) | low;
}

// Write a value in 64-bit to MSR
static inline void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    asm volatile(
        "wrmsr"
        :
        : "c"(msr), "a"(low), "d"(high));
}

static inline uint64_t read_cr0()
{
    uint64_t val;
    asm volatile(
        "mov %%cr0, %0"
        : "=r"(val)
        :
        : "memory");

    return val;
}

static inline void write_cr0(uint64_t val)
{
    asm volatile(
        "mov %0, %%cr0"
        :
        : "r"(val)
        : "memory");
}

static inline uint64_t read_cr1()
{
    uint64_t val;
    asm volatile(
        "mov %%cr1, %0"
        : "=r"(val)
        :
        : "memory");

    return val;
}

static inline void write_cr1(uint64_t val)
{
    asm volatile(
        "mov %0, %%cr1"
        :
        : "r"(val)
        : "memory");
}

static inline uint64_t read_cr2()
{
    uint64_t val;
    asm volatile(
        "mov %%cr2, %0"
        : "=r"(val)
        :
        : "memory");

    return val;
}

static inline void write_cr2(uint64_t val)
{
    asm volatile(
        "mov %0, %%cr2"
        :
        : "r"(val)
        : "memory");
}

static inline uint64_t read_cr3()
{
    uint64_t val;
    asm volatile(
        "mov %%cr3, %0"
        : "=r"(val)
        :
        : "memory");

    return val;
}

static inline void write_cr3(uint64_t val)
{
    asm volatile(
        "mov %0, %%cr3"
        :
        : "r"(val)
        : "memory");
}

static inline uint64_t read_cr4()
{
    uint64_t val;
    asm volatile(
        "mov %%cr4, %0"
        : "=r"(val)
        :
        : "memory");

    return val;
}

static inline void write_cr4(uint64_t val)
{
    asm volatile(
        "mov %0, %%cr4"
        :
        : "r"(val)
        : "memory");
}

// --- SSE Setup ---

#define CR0_MP (1 << 1)          // monitor coprocessor
#define CR0_EM (1 << 2)          // emulation
#define CR4_OSFXSR (1 << 9)      // OS support for FSXAVE/FSRSTOR
#define CR4_OSXMMEXCPT (1 << 10) // OS support for Unmasked SIMD FPU Exceptions

static inline void enable_sse(void)
{
    // set MP (we control the FPU)
    // and clear EM (we use hardware FPU, not software)
    uint64_t cr0 = read_cr0();
    cr0 &= ~CR0_EM;
    cr0 |= CR0_MP;
    write_cr0(cr0);

    // set OSFXSR to se fxsave/fxrstor for FPU context
    // and set OSXMMEXCPT to handle related math
    uint64_t cr4 = read_cr4();
    cr4 |= CR4_OSFXSR;
    cr4 |= CR4_OSXMMEXCPT;
    write_cr4(cr4);
}

#endif