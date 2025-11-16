/* pit.c - Programmable Interval Timer (PIT) driver
 *
 * - The PIT is clocked by a constant oscillator (PIT_INPUT_CLOCK). To obtain a
 *   desired interrupt frequency we program a 16-bit divisor = PIT_INPUT_CLOCK / freq.
 * - The PIT provides channel registers accessed via I/O ports:
 *     - 0x43 : Control/command register
 *     - 0x40 : Channel 0 data port (low byte then high byte)
 * - The divisor must be in range [1, 65535]. A divisor of 0 is not allowed.
 * - After programming the PIT you must unmask IRQ0 on the PIC so interrupts
 *   can reach the CPU. This implementation clears the mask for IRQ0.
 */

#include "pit.h"
#include "arch/irq.h"
#include "pic.h"
#include "io.h"

#define PIT_DATA_0 0x40
#define PIT_DATA_1 (PIT_DATA_0+1)
#define PIT_DATA_2 (PIT_DATA_0+2)
#define PIT_COMMAND (PIT_DATA_0+3)
#define PIT_CONTROL_BYTE 0x36    /* channel0, lobyte/hibyte, mode3, binary */
#define PIT_INPUT_CLOCK  1193182 /* Hz */

static volatile uint64_t ticks = 0; /* 64-bit tick counter used by pit_get_ticks */

/* IRQ handler for PIT (called via irq_dispatch). Increment tick counter only.
 * irq_dispatch will send the PIC EOI after calling this handler.
 */
void pit_handler(void *regs)
{
    (void)regs;
    ticks++;
}

/* Initialize PIT to `freq_hz` ticks per second.
 * - Returns immediately if freq_hz == 0 (no programming).
 * - Clamps divisor to [1, 0xFFFF].
 * - Registers IRQ0 handler and unmasks IRQ0.
 */
void pit_init(uint32_t freq_hz)
{
    if (freq_hz == 0)
        return;

    uint32_t divisor = PIT_INPUT_CLOCK / freq_hz;
    if (divisor == 0)
        divisor = 1;
    if (divisor > 0xFFFF)
        divisor = 0xFFFF;

    uint16_t d = (uint16_t)divisor;

    outb(PIT_COMMAND, PIT_CONTROL_BYTE);
    outb(PIT_DATA_0, d & 0xFF);         /* low byte */
    outb(PIT_DATA_0, (d >> 8) & 0xFF);  /* high byte */

    register_irq_handler(0, pit_handler);

    asm volatile ("cli");
    pic_clear_mask(0);
    asm volatile ("sti");
}

uint32_t pit_get_ticks(void)
{
    return ticks;
}