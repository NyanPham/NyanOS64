#ifndef CPU_H
#define CPU_H

#include <stdint.h>

static inline void cpuid(uint32_t code, uint32_t* eax, uint32_t* ecx, uint32_t* edx, uint32_t* ebx)
{
    asm volatile(
        "cpuid"
        : "=a"(*eax), "=c"(*ecx), "=d"(*edx), "=b"(*ebx)
        : "a"(code)
    );
}

// Read the MSR in 64-bit
static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t low, high;
    asm volatile(
        "rdmsr"
        : "=a"(low), "=d"(high)
        : "c"(msr)
    );
    
    return ((uint64_t)high << 32) | low;
}

// Write a value in 64-bit to MSR
static inline void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    asm volatile (
        "wrmsr"
        : 
        : "c"(msr), "a"(low), "d"(high)
    );
}

static inline uint64_t read_cr3()
{
    uint64_t val;
    asm volatile (
        "mov %%cr3, %0" 
        : "=r"(val)
        :
        : "memory"
    );

    return val;
}

static inline void write_cr3(uint64_t val)
{
    asm volatile (
        "mov %0, %%cr3" 
        : 
        : "r"(val)
        : "memory"
    );
}

#endif