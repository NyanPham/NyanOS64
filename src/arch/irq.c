#include "irq.h"
#include "drivers/apic.h"
// #include "pic.h"

// An array of IRQ handlers, one for each of the 16 IRQ lines.
static irq_handler_t irq_handlers[16];

// Registers a handler for a given IRQ.
void register_irq_handler(int irq, irq_handler_t handler)
{
    if (irq < 0 || irq >= 16)
    {
        return;
    }

    irq_handlers[irq] = handler;
}

// This function is called by the assembly IRQ stubs.
// It calls the registered handler for the given IRQ and then sends an EOI to the PIC.
void irq_dispatch(int irq, void *regs)
{
    if (irq < 0 || irq >= 16) 
    {
        return; 
    }

    if (irq_handlers[irq]) 
    {
        irq_handlers[irq](regs);
    }

    lapic_send_eoi();
}

// This is the main entry point for all IRQs.
// It's called from the assembly stub and is responsible for dispatching the IRQ to the correct handler.
void irq_entry(void *regs, int irq)
{
    irq_dispatch(irq, regs);
}