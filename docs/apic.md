# APIC

## Definition

APIC ("Advanced Programmable Interrupt Controller") is the updated Intel standard for the older PIC.
It's used for sophisticated interrupt redirection, and for sending interrupts between processors.

## Detection

CPUID.01h:EDX[bit 9] flags specifies if a CPU has a built-in local APIC.

## Local APIC and IO-APIC

In an APIC-based system, each CPU has a **core** and a **local APIC**. The APIC is to handle cpu-specific interrupt configuration, and contains _Local Vector Table (LVT)_, which translates events such as "internal clock" and other "local" interrupt sources into an interrupt vector.

There is also an I/O APIC. In systems with multiple I/O subsytems, each subsystem can have its own set of interrupts.

Each interrupt pin is programmable as either edge or level triggered:

- Edge-triggered interrupts are activated by a change in signal (typically a rising or falling edge).
- Level-triggered interrupts remain active as long as the signal is held at a certain level (e.g., high or low).
  We can relocate the I/O APIC's two-register memory space to another memory space, defaults to 0xFEC00000.

## Local APIC configuration

Local APIC is enabled at boot-time, and can be disabled by clearing bit 11 of the IA32_APIC_BASE [Model Specific Register (MSR)](msr.md).

Note that the Intel Software Developer's Manual states that, once we have disabled the local APIC, we have no way to enable it again until a complete reset.

The local APIC's registers are memory-mapped in physical page 0xFEE00xxx. This address is fixed, so we can only access the registers of the local APIC that our code is running on.

Multiple APIC Description Table (MADT) contains the local APIC base, on 64-bit, it possibly contains a field that specifies the base address override. Or we can leave the base where we find it.

To enable the local APIC to receive interrupts, we need to configure the "Spurious Interrupt Vector Register". We choose an interrupt number (IRQ) for handling spurious interrupts, and put that nuimber in the lowest 8 bits of the register. Also, make sure the 8th bit is turned on. The chosen interrupt number should have its lowest 4 bits set (for legacy) and is above 32 (0xFF is the perfect choice).

To disable the local APIC, we do 2 steps:

1. Masking all the interrupts (to disable all of them in PIC)
2. Remapping the IRQs (to 32 instead of 0 to avoid conflicts with the CPU exceptions).

Example code to set up the APIC:

```c
#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_BSP 0x100
#define IA32_APIC_BASE_MSR_ENABLE 0x800

/** returns a 'true' value if the CPU supports APIC
 *  and if the local APIC hasn't been disabled in MSRs
 *  note that this requires CPUID to be supported.
 */

bool check_apic()
{
    uint32_t eax, edx;
    cpuid(1, &eax, &edx);   // 1 to get processor info and feature bits 
    return edx & CPUID_FEAT_EDX_APIC;
}

/* Set the physical address for local APIC registers */
void cpu_set_apic_base(uintptr_t apic)
{
    uint32_t edx = 0;
    uint32_t eax = (apic & 0xfffff0000) | IA32_APIC_BASE_MSR_ENABLE;

#ifdef __PHYSICAL_MEMORY_EXTENSION__
    edx = (apic >> 32) & 0x0f;
#endif

    cpu_set_msr(IA32_APIC_BASE_MSR, eax, edx);
}

/**
 * Get the physical address of the APIC registers page
 * make sure you map it to virtual memory ;)
 */
uintptr_t cpu_get_apic_base()
{
   uint32_t eax, edx;
   cpu_get_msr(IA32_APIC_BASE_MSR, &eax, &edx);

#ifdef __PHYSICAL_MEMORY_EXTENSION__
   return (eax & 0xfffff000) | ((edx & 0x0f) << 32);
#else
   return (eax & 0xfffff000);
#endif
}

void enable_apic() {
    /* Section 11.4.1 of 3rd volume of Intel SDM recommends mapping the base address page as strong uncacheable for correct APIC operation. */

    /* Hardware enable the Local APIC if it wasn't enabled */
    cpu_set_apic_base(cpu_get_apic_base());

    /* Set the Spurious Interrupt Vector Register bit 8 to start receiving interrupts */
    write_reg(0xF0, ReadRegister(0xF0) | 0x100);
}
```

## Local APIC and x86 SMM Attacks

The APIC was introduced the same time with System Management Mode. Later on, System Management Mode's memory (SMRAM) is mappable to other location, which can be outside the protected range. Attackers can laverage their permissions using SMM, which is protected from all rings above -2.

Since Intel Atom in 2013, this has been solve partly, as it still relies on the SMMR to be configured correctly.

## Local APIC registers

The registers are mapped to an address that is found in MP/MADT.
Using paging, we need to map these to Virtual memory.
Each register is 32-bit long, or 4 bytes, and they are 16 byte aligned.

| Offset    | Register Name                                                    | Read/Write Permissions |
| --------- | ---------------------------------------------------------------- | ---------------------- |
| 000h-010h | Reserved                                                         |                        |
| 020h      | LAPIC ID Register                                                | Read/Write             |
| 030h      | LAPIC Version Register                                           | Read only              |
| 040h-070h | Reserved                                                         |                        |
| 080h      | Task Priority Register (TPR)                                     | Read/Write             |
| 090h      | Arbitration Priority Register (APR)                              | Read only              |
| 0A0h      | Processor Priority Register (PPR)                                | Read only              |
| 0B0h      | EOI register                                                     | Write only             |
| 0C0h      | Remote Read Register (RRD)                                       | Read only              |
| 0D0h      | Logical Destination Register                                     | Read/Write             |
| 0E0h      | Destination Format Register                                      | Read/Write             |
| 0F0h      | Spurious Interrupt Vector Register                               | Read/Write             |
| 100h-170h | In-Service Register (ISR)                                        | Read only              |
| 180h-1F0h | Trigger Mode Register (TMR)                                      | Read only              |
| 200h-270h | Interrupt Request Register (IRR)                                 | Read only              |
| 280h      | Error Status Register                                            | Read only              |
| 290h-2E0h | Reserved                                                         |                        |
| 2F0h      | LVT Corrected Machine Check Interrupt (CMCI) Register Read/Write |
| 300h-310h | Interrupt Command Register (ICR)                                 | Read/Write             |
| 320h      | LVT Timer Register                                               | Read/Write             |
| 330h      | LVT Thermal Sensor Register                                      | Read/Write             |
| 340h      | LVT Performance Monitoring Counters Register                     | Read/Write             |
| 350h      | LVT LINT0 Register                                               | Read/Write             |
| 360h      | LVT LINT1 Register                                               | Read/Write             |
| 370h      | LVT Error Register                                               | Read/Write             |
| 380h      | Initial Count Register (for Timer)                               | Read/Write             |
| 390h      | Current Count Register (for Timer)                               | Read only              |
| 3A0h-3D0h | Reserved                                                         |                        |
| 3E0h      | Divide Configuration Register (for Timer)                        | Read/Write             |
| 3F0h      | Reserved                                                         |                        |

## EOI Register

Write to the register with offset 0xB0 using the value 0 to signal an end of interrupt.
A non-zero value may cause a general protection fault.

## Local Vector Table Registers

Some interrupts must be configured using registers in LAPIC, not I/O APIC.
Focus on:

- lapic timer: 0x320
- lint0: 0x350
- lint1: 0x360

Register format:
| Bits                      | Description                                      |
|---------------------------|--------------------------------------------------|
| 0–7                       | The vector number                                |
| 8–10                      | Reserved for timer; `100b` if NMI                |
| 11                        | Reserved                                         |
| 12                        | Set if interrupt is pending                      |
| 13 (reserved for timer)   | Polarity; set means active low                  |
| 14 (reserved for timer)   | Remote IRR                                       |
| 15 (reserved for timer)   | Trigger mode; set means level-triggered         |
| 16                        | Set to mask the interrupt                        |
| 17–31                     | Reserved                                         |

## Spurious Interrupt Vector Register

The offset is 0xF0.
The low byte contains the number of the spurious interrupt (should be 0xFF).
Set the bit 8 (or 0x100) to enable the APIC.
If bit 12 is set, EOI messages won't be broadcast.
All the other bits are reserved.

## Interrupt Command Register

It is made up of 2 32-bit registers: at 0x300 and 0x310.
It's used to send interrupts to different processors.
To send an interrupt command, first write to 0x310, then to 0x300.
The 0x310's bits 24-27 is the local APIC ID field.

| Bits  | Description                                                                                                                         |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------- |
| 0–7   | Vector number, or starting page number for SIPIs                                                                                    |
| 8–10  | Delivery mode: 0 = normal, 1 = lowest priority, 2 = SMI, 4 = NMI, 5 = INIT or INIT level de-assert, 6 = SIPI                        |
| 11    | Destination mode: 0 = physical, 1 = logical. If 0, destination field in 0x310 is used normally.                                     |
| 12    | Delivery status: Cleared when the interrupt is accepted by the target. Wait for this bit to clear after sending an interrupt.       |
| 13    | Reserved                                                                                                                            |
| 14    | Clear for INIT level de-assert, otherwise set.                                                                                      |
| 15    | Set for INIT level de-assert, otherwise clear.                                                                                      |
| 18–19 | Destination type: >0 means destination field in 0x310 is ignored. 1 = self, 2 = all processors, 3 = all except self. Best to use 0. |
| 20–31 | Reserved                                                                                                                            |

## IO APIC Configuration
There are 2 registers:
- Address register: IOAPICBASE + 0, the bottom 8-bits to select register
- Data register: IOAPICBASE + 0x10

Memory accesses must be 4-byte aligned.

How to read from and write to IO APIC registers?
As said, we use a pair of registers: 
IOREGSEL (offset 0) to select which IO APIC register we want to access, 
and IOWIN (offset 0x10, or at index 4 (0x10 / 4 bytes = 4)) to read/write the selected register.

```c
uint32_t cpu_read_io_apic(void *ioapicaddr, uint32_t reg)
{
    uint32_t volatile *ioapic = (uint32_t volatile *)ioapicaddr;
    ioapic[0] = reg & 0xff; // select the register
    return ioapicp[4]; // read the value
}

void cpu_write_io_apic(void *ioapicaddr, uint32_t reg, uint32_t value)
{
    uint32_t volatile *ioapic = (uint32_t volatile *)ioapicaddr;
    ioapic[0] = reg & 0xff; // select the register
    ioapic[4] = value;  // write the value
}
```

## IO APIC Registers
OK, we know how to read and write registers. Here are the list of accessiable registers:

| Offset     | Description |
|------------|-------------|
| 0x00       | Get/set the IO APIC's id in bits 24-27. All other bits are reserved. |
| 0x01       | Get the version in bits 0-7. Get the maximum amount of redirection entries in bits 16-23. All other bits are reserved. Read only. |
| 0x02       | Get the arbitration priority in bits 24-27. All other bits are reserved. Read only. |
| 0x10-0x3F  | Contains a list of redirection entries. They can be read from and written to. Each entries uses two addresses, e.g. 0x12 and 0x13. |

Redirection entry bits:

| Bits     | Description |
|----------|-------------|
| 0-7      | Interrupt vector. Allowed values are from 0x10 to 0xFE. |
| 8-10     | Type of delivery mode. 0 = Normal, 1 = Low priority, 2 = System management interrupt, 4 = Non maskable interrupt, 5 = INIT, 7 = External. All others are reserved. |
| 11       | Destination mode. Affects how the destination field is read, 0 is physical mode, 1 is logical. If the Destination Mode of this entry is Physical Mode, bits 56-59 contain an APIC ID. |
| 12       | Set if this interrupt is going to be sent, but the APIC is busy. Read only. |
| 13       | Polarity of the interrupt. 0 = High is active, 1 = Low is active. |
| 14       | Used for level triggered interrupts only to show if a local APIC has received the interrupt (= 1), or has sent an EOI (= 0). Read only. |
| 15       | Trigger mode. 0 = Edge sensitive, 1 = Level sensitive. |
| 16       | Interrupt mask. Stops the interrupt from reaching the processor if set. |
| 17-55    | Reserved. |
| 56-63    | Destination field. If the destination mode bit was clear, then the lower 4 bits contain the bit APIC ID to sent the interrupt to. If the bit was set, the upper 4 bits also contain a set of processors. |

The table above lets us choose which external interrupts to send to which processors and with which interupts vectors.
When choosing the processors, consider: 
- spread out the workload between the processors
- avoid processors in low-power state
- avoid throttled processors

When choosing the interrupt vectors, remember:
- interrupts 0x00 to 0x1F are reserved for internal processor exceptions
- interrupts we remapped the PIC to may receive spurious interrupts
- 0xFF is probably where we put the APIC spurious interrupt
- the upper 4 bits of an interrupt vector indicate its priority

## Local Destination Mode
Logical destination mode lets you send interrupts to a group of processors at once. Each processor has a special register called the Logical Destination Register (LDR) that holds an 8-bit ID. When an interrupt is sent, all processors check if their ID matches the one in the interrupt message. If it does, they respond to it. By giving multiple processors the same ID, you can easily target them as a group.


LDR's layout:
| Bits        | Mode          | Description                                                                 |
|-------------|---------------|-----------------------------------------------------------------------------|
| 0–23        | All           | Reserved                                                                    |
| 24–31       | Flat model    | Bitmap of target processors (each bit identifies a single processor; supports up to 8 local APIC units) |
| 24–27       | Cluster model | Local APIC address (identifies the specific processor in a group)           |
| 28–31       | Cluster model | Cluster address (identifies a group of processors)                          |

Destination format register (DFR) tells us it's a Flat or Cluster model, and is structured as follows:

| Bits     | Description                                      |
|----------|--------------------------------------------------|
| 0–27     | Reserved                                         |
| 28–31    | Model type (1111b = Flat model, 0000b = Cluster model) |

OS for 64-bit processors typically uses "Flat model", and the system usually have 8 CPUs.
If more than 8 CPUs is used, some OSes use cluster model which allow to address 60 CPUs in total.

In logical delivery mode, you can easily send interrupts to multiple CPUs at once. For example, setting the cluster ID to 0xf sends the interrupt to all clusters (a broadcast). The system can have up to 15 clusters, each with 4 CPUs.

This mode is especially useful for sending inter-processor interrupts (IPIs) or when using the lowest priority delivery mode. In that case, the interrupt (like one from an MSI or IO-APIC) is sent to whichever CPU in the target group is least busy, based on the destination field.