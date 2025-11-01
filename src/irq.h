#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

typedef void (*irq_handler_t)(void *regs);

void register_irq_handler(int irq, irq_handler_t handler);
void irq_dispatch(int irq, void *regs);
void irq_entry(void *regs, int irq); 

#endif