# IDT

To use IDT, GDT needs to be loaded.

## Terms:
- Interrupt Descriptor Table: an array of interrupt descriptors
- Interrupt Descriptor: an entry in IDT, describing what the CPU should do to handle an interrupt.
- Interrupt Descriptor Table Register: IDTR, holding the address of the IDT, similar to GDTR.
- Interrupt Vector: Interrupt number, or ID. Vectors 0-32 are reserved.
- Interrupt Request: a term describing an interrupt that is already sent to the Programmable Interrupt Controller (PIC, APIC). IRQ are the pin numbers used on the PIC. 
- Interrupt Service Routine: ISR, a handler to serve an IRQ.

In short, we build a table of descriptors, each descriptor is handle a specific interrupt, and load that table into IDTR.

## Interrupt Descriptors

```c
struct interrupt_descriptor
{
    uint16_t address_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t address_mid;
    uint32_t address_high;
    uint32_t reserved;
} __attribute__((packed));
```

`address_`: A handler is at an 64-bit address. We split the address up into 3 parts: low, mid and high.

`selector`: a code selecotr to run the interrupt handler. This should be our kernel code selector.

`ist`: can be ignored for now, as it's for edge cases like to handle NMIs.

`flags`: a bitfield:
```
---------------------------------------------------------------
|    7    | 6 : 5  |       4      |           3 : 0           |
---------------------------------------------------------------
| Present | DPL: 0 | Reserved : 0 |  Type: trap or interrupt  |
---------------------------------------------------------------
```

Difference between interrupt gates and trap gates:
- Interrupt gates (0b1110) clears the interrupt flag
- Trap gates (0b1111) doens't 

That means trap gates are useful when we allow interrupts happen when dealing with traps.

## Interrupt Handler Stub
There are some info that need storing on the stack before calling the handler,
because we need a place to return after handling an interrupt.

- ss: stack selector
- rsp: stack top
- rflags
- cs: code selector
- rip

The CPUs do the push and pop of these values for us already.

However, the CPU does **not** save general-purpose registers (like `rax`, `rbx`, etc.). This is why we need an assembly "stub" for each interrupt. The stub's job is to:
1. Save all general-purpose registers to the stack.
2. Call the C handler function.
3. After the C function returns, restore all general-purpose registers.
4. Execute `iretq` to return from the interrupt.

### Error Codes

Some exceptions (like #8, #10, #11, #12, #13, #14, #17, #21) push an 8-byte "error code" onto the stack after the RIP. Our interrupt handler stub must account for this value so that the stack is correctly managed. For interrupts that *don't* push an error code, we often push a dummy value of 0 so that all handlers have a consistent stack frame.
