#include "keyboard.h"
#include "io.h"
#include "irq.h"
#include "pic.h"

static volatile uint8_t scancode = 0;

static void keyboard_handler(void *regs)
{
    (void)regs;
    scancode = inb(0x60);
    // TODO: add output for testing

    pic_send_eoi(1);
}

void keyboard_init(void)
{
    register_irq_handler(1, keyboard_handler);
    pic_clear_mask(1);
}