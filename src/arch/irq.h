#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

// A function pointer type for IRQ handlers.
// The `regs` parameter is a pointer to the saved registers on the stack.
typedef void (*irq_handler_t)(void *regs);

// Registers an IRQ handler for the given IRQ number.
void register_irq_handler(int irq, irq_handler_t handler);

// Dispatches an IRQ to the appropriate handler.
void irq_dispatch(int irq, void *regs);

// The main entry point for all IRQs.
void irq_entry(void *regs, int irq); 

#endif