#include "timer.h"
#include "arch/irq.h"

static volatile uint64_t g_ticks = 0;

static void timer_handler(void* regs)
{
    (void)regs;
    g_ticks++;
}

void timer_init()
{
    register_irq_handler(0, timer_handler);
}

uint64_t timer_get_ticks()
{
    return g_ticks;
}