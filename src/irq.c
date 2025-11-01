#include "irq.h"
#include "pic.h"

static irq_handler_t irq_handlers[16];

void register_irq_handler(int irq, irq_handler_t handler)
{
    if (irq < 0 || irq >= 16)
    {
        return;
    }

    irq_handlers[irq] = handler;
}

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

    pic_send_eoi((uint8_t)irq);
}

void irq_entry(void *regs, int irq)
{
    irq_dispatch(irq, regs);
}