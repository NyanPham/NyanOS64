# Model Specific Registers


## Definition
They are the collection of registers that allow configuration of OS-relevant things such as memory type-range, sysenter/sysexit, local APIC, etc.

## Accessing Model Specific Registers
MSRs are 64-bit wide.
The presence of MSRs is indicated by CPUID.01h:EDX[bit 5]:

```C
const uint32_t CPUID_FLAG_MSR = 1 << 5;

bool cpu_has_msr()
{
    static uint32_t a, d;
    cpuid(1, &a, &d);
    return d & CPUID_FLAG_MSR;
}

void cpu_get_msr(uin32_t msr, uint32_t *lo, uint32_t *hi)
{
    asm volatile("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(msr));
}

void cpu_set_msr(uint32_t msr, uint32_t lo, uint32_t hi)
{
    asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}
```

## Other way to access MSRs
`rdmsr` and `wrmsr` are privileged instructions.
`rdtsc` instruction is a non-privileged instruction that reads the timestamp counter.