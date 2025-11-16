#include "keyboard.h"
#include "io.h"
#include "arch/irq.h"
#include "serial.h"
// #include "pic.h"

static volatile uint8_t scancode = 0;

static void keyboard_handler(void *regs)
{
    (void)regs;
    scancode = inb(0x60);
    
    kprint("KEYPRESS: Scancode: ");
    kprint_hex_32(scancode);
    kprint("\n");
}

void keyboard_init(void)
{
    register_irq_handler(1, keyboard_handler);
    // pic_clear_mask(1);
}