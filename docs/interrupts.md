# Interrupts

Firstly, let's briefly talk about *Interrupts*.
Interrupts are to let the CPU communicate to our code that something unexpected/expected has happened, and we need to handle that.

Our code needs to have interrupt handler to serve a specific interrupt.

There are 3 main types of interrupts:
1.  **Hardware Interrupts (IRQs):** Asynchronous interrupts from external devices like the keyboard or timer.
2.  **Software Interrupts:** Triggered intentionally by code using an instruction like `int n`.
3.  **Exceptions:** Synchronous interrupts triggered by the CPU when an error occurs (e.g., division by zero, page fault).

A handler is just a subroutine, a function with some conditions.

## Flags
Sometimes, the OS needs privacy, and doesn't want to be interrupted by no means. We have flags to disable interrupts. And of course, we can enable it back.
- cli: clear interrupt flag
- sti: set interrupt flag

## Non-Maskable Interrupts
Please read [nmi.md](./nmi.md)
We don't actually care much about NMI.

# IDT
Please read [idt.md](./idt.md)