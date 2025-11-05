/* PIT (Programmable Interval Timer) interface.
 *
 * This header declares the public API for the PIT driver used by the kernel.
 * The PIT is the legacy 8253/8254 timer chip driven by a 1.193182 MHz clock.
 */
#ifndef PIT_H
#define PIT_H

#include <stdint.h>

/* PIT (8253/8254) interface.
 * - pit_init(freq_hz): program channel 0 to generate periodic IRQ0 at `freq_hz`.
 * - pit_get_ticks():  return monotonic tick counter (32-bit).
 */
void pit_init(uint32_t freq_hz);
uint32_t pit_get_ticks(void);

#endif