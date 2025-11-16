#ifndef PIC_H
#define PIC_H

#include <stdint.h>

/**
 * @file pic.h
 * @brief Interface for the 8259 Programmable Interrupt Controller (PIC).
 * This handles remapping and managing hardware interrupts (IRQs).
 */

// IO port addresses for the master and slave PICs.
#define PIC1		0x20		/* IO base address for master PIC */
#define PIC2		0xA0		/* IO base address for slave PIC */
#define PIC1_COMMAND	PIC1
#define PIC1_DATA	(PIC1+1)
#define PIC2_COMMAND	PIC2
#define PIC2_DATA	(PIC2+1)

#define PIC_EOI		0x20		/* End-of-interrupt command code */

/**
 * @brief Initializes and remaps the master and slave PICs.
 * The PICs are remapped to avoid conflicts with CPU exception vectors.
 * This should be called once during kernel initialization.
 */
void pic_init(void);

/**
 * How Masking and EOI Work Together
 *
 * The PIC's interrupt lines can be individually enabled or disabled using a
 * register called the Interrupt Mask Register (IMR).
 *
 * - `pic_clear_mask(irq)`: To **enable** interrupts for a specific device (e.g., IRQ 1
 *   for the keyboard), you must clear its corresponding bit in the IMR. This
 *   tells the PIC "I am ready to receive interrupts on this line." This is
 *   typically done once during device initialization.
 *
 * - `pic_set_mask(irq)`: To **disable** interrupts for a device, you set its
 *   bit in the IMR. The PIC will then ignore any signals from that device.
 *
 * - `pic_send_eoi(irq)`: When an enabled interrupt occurs and your handler has
 *   finished its job, you must send an End-of-Interrupt signal. This clears
 *   the interrupt's bit in the In-Service Register (ISR), telling the PIC that
 *   the handler is complete and it can now generate new interrupts for devices
 *   at the same or lower priority levels. **Forgetting to send EOI will block
 *   further interrupts and hang the system.**
 *
 * Typical Flow for a Keyboard Interrupt (IRQ 1):
 * 1. `pic_clear_mask(1)` is called once during keyboard driver initialization.
 * 2. The user presses a key, triggering IRQ 1.
 * 3. The `keyboard_handler` runs and reads the scancode from the I/O port.
 * 4. `pic_send_eoi(1)` is called at the end of the handler.
 * 5. The system is now ready for the next keyboard interrupt.
 */

/**
 * @brief Sends the End-of-Interrupt (EOI) signal to the PIC(s).
 * This must be called at the end of an interrupt handler to signal that the
 * interrupt has been processed. Failing to do so will prevent further
 * interrupts from the same or lower-priority devices.
 * @param irq The IRQ number that was handled.
 */
void pic_send_eoi(uint8_t irq);

/**
 * @brief Masks an IRQ line, effectively disabling it.
 * The PIC will ignore any requests from a masked IRQ line.
 * @param irq The IRQ line (0-15) to disable.
 */
void pic_set_mask(uint8_t irq);

/**
 * @brief Clears the mask for an IRQ line, effectively enabling it.
 * This allows the PIC to receive and forward interrupt requests from this line.
 * @param irq The IRQ line (0-15) to enable.
 */
void pic_clear_mask(uint8_t irq);

/**
 * @brief Disables the PIC entirely by masking all IRQs.
 * This is useful when transitioning to a more modern interrupt controller like the APIC.
 */
void pic_disabled(void);

/**
 * Anatomy of a PIC Interrupt: The IRR and ISR Flow
 *
 * The IRR (Interrupt Request Register) and ISR (In-Service Register) are two
 * internal 8-bit registers on each PIC chip that work together to manage
 * interrupt handling.
 *
 * 1.  **Request Arrives**: A hardware device (e.g., keyboard) triggers its
 *     IRQ line. The PIC sets the corresponding bit in the **IRR**. This means
 *     "an interrupt is waiting".
 *
 * 2.  **PIC Signals CPU**: The PIC signals the CPU that an interrupt is pending.
 *
 * 3.  **CPU Acknowledges**: The CPU finishes its current instruction and asks the
 *     PIC for the interrupt vector number.
 *
 * 4.  **Registers Update**: When the PIC sends the vector to the CPU, it
 *     atomically:
 *     - Clears the bit in the **IRR** (the request is no longer "waiting").
 *     - Sets the corresponding bit in the **ISR** (the request is now "being serviced").
 *
 * 5.  **EOI (End of Interrupt)**: The bit in the ISR remains set until the OS's
 *     interrupt handler sends an `EOI` command back to the PIC. The EOI clears
 *     the bit in the **ISR**, signaling that the interrupt is finished and the PIC
 *     can now service other interrupts of the same or lower priority.
 */

/**
 * @brief Reads the combined Interrupt Request Register (IRR) from both PICs.
 * The IRR indicates which IRQs have been raised but are waiting to be serviced.
 */
uint16_t pic_get_irr(void);

/**
 * @brief Reads the combined In-Service Register (ISR) from both PICs.
 * The ISR indicates which IRQs are currently being serviced.
 */
uint16_t pic_get_isr(void);

#endif