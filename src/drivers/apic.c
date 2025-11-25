#include "apic.h"
#include "legacy/pic.h"
#include "../cpu.h"
#include "mem/vmm.h"
#include "serial.h" // for debugging with kprint

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CPUID_FEAT_EDX_APIC (1 << 9)

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_BSP 0x100
#define IA32_APIC_BASE_MSR_ENABLE 0x800 // Bit 11, APIC ENABLE

#define DEFAULT_IOAPICBASE 0xFEC00000
#define SPURIOUS_INT_REG_OFFSET 0xF0
#define EOI_REG_OFFSET 0xB0
#define TIMER_OFFSET 0x320
#define INITIAL_COUNT_OFFSET 0x380
#define DIVIDE_CONFIG_OFFSET 0x3E0
#define TPR_OFFSET 0x80

extern uint64_t hhdm_offset;
extern uint64_t* kern_pml4;
static volatile uint32_t* g_ioapic_base = NULL;
static volatile uint32_t* g_lapic_regs = NULL;

/** returns a 'true' value if the CPU supports APIC
 *  and if the local APIC hasn't been disabled in MSRs
 *  note that this requires CPUID to be supported.
 */
bool check_apic()
{
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ecx, &edx, &ebx);   // 1 to get processor info and feature bits 
    return edx & CPUID_FEAT_EDX_APIC;
}

// Read a 32-bit reg from I/O APIC
static uint32_t ioapic_read(uint32_t reg)
{
    g_ioapic_base[0] = reg & 0xFF;
    return g_ioapic_base[4];
}

// Write a 32-bit reg to I/O APIC
static void ioapic_write(uint32_t reg, uint32_t val)
{
    g_ioapic_base[0] = reg & 0xFF;
    g_ioapic_base[4] = val;
}

void apic_init()
{
    if (!check_apic())
    {
        kprint("CPU doesn't support APIC!\n");
        return;
    }

    pic_disabled();
    uint64_t base = rdmsr(IA32_APIC_BASE_MSR);
    
    // check if the Bit 11(APIC Enable) is on
    if (!(base & IA32_APIC_BASE_MSR_ENABLE))
    {
        kprint("APIC is not enabled!\n");
        return;
    }

    /*
    Notes:
    IA32_APIC_BASE_MSR, DEFAULT_IOAPICBASE are not in the map provided in the Limine map, because
    they don't exist in the USABLE map, they are Memory-maped IO.
    So we need to map their phys_addr and virt_addr manually.
    */

    // get phys_addr and virt_addr of IA32_APIC_BASE_MSR, and do mapping
    uint64_t phys_addr = base & (~(0xFFF));
    uint64_t virt_addr = phys_addr + hhdm_offset;
    vmm_map_page(kern_pml4, virt_addr, phys_addr, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE);

    g_lapic_regs = (volatile uint32_t*)virt_addr;

    // Enable LAPIC in the CPU
    g_lapic_regs[SPURIOUS_INT_REG_OFFSET / sizeof(uint32_t)] |= (IA32_APIC_BASE_BSP | 0xFF);
    g_lapic_regs[TPR_OFFSET / sizeof(uint32_t)] = 0;

    // Timer
    g_lapic_regs[TIMER_OFFSET / sizeof(uint32_t)] = (0x20 | (1 << 17)); // Set up the timer, 0x20 is our irq0_stub, bit 17 is the "Periodic" mode
    g_lapic_regs[DIVIDE_CONFIG_OFFSET / sizeof(uint32_t)] |= 0x03;  // divide config to /16
    g_lapic_regs[INITIAL_COUNT_OFFSET / sizeof(uint32_t)] = 0x10000;// initial count

    // get the phys_addr and virt_addr of DEFAULT_IOAPICBASE, and do mapping
    uint64_t ioapic_phys = DEFAULT_IOAPICBASE;
    uint64_t ioapic_virt = ioapic_phys + hhdm_offset;
    vmm_map_page(kern_pml4, ioapic_virt, ioapic_phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE);

    g_ioapic_base = (volatile uint32_t*)ioapic_virt;

    // wire up the keyboard handler
    ioapic_write(0x12, 0x21);
    ioapic_write(0x13, 0x00);

    // Test, read the I/O APIC chip's version at the 0x01 reg
    kprint("Version is: ");
    kprint_hex_32(ioapic_read(0x01));
    kprint("\n");
}

void lapic_send_eoi(void)
{
    g_lapic_regs[EOI_REG_OFFSET / sizeof(uint32_t)] = 0;
}