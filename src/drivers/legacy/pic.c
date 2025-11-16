#include "pic.h"
#include "io.h"

/*
 * The 8259 PIC has a master and a slave, each with 8 inputs.
 * The master's inputs are IRQ0-IRQ7, and the slave's are IRQ8-IRQ15.
 * By default, these map to interrupt vectors 8-15 and 16-23, respectively.
 * This conflicts with the CPU's exception vectors, so we need to remap them.
 */

// Initialization Command Word 1
// This is the first command sent to the PIC. It tells the PIC whether to expect
// ICW4, whether it's in single (only one PIC is used) 
// or cascade mode (master + slave; master connects to CPU, 
// slave connects one line to the master, usually at IRQ2), 
// and the interrupt vector address interval.
#define ICW1_ICW4	0x01		/* Indicates that ICW4 will be present */
#define ICW1_SINGLE	0x02		/* Single (cascade) mode */
#define ICW1_INTERVAL4	0x04	/* Call address interval 4 (8) */
#define ICW1_LEVEL	0x08		/* Level triggered (edge) mode */
#define ICW1_INIT	0x10		/* Initialization - required! */

// Initialization Command Word 4
// This is the last command sent to the PIC. It tells the PIC about the CPU
// mode (8086/88 or MCS-80/85), whether to automatically send EOI, and the
// buffering mode.
#define ICW4_8086   0x01		/* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO	0x02		/* Auto (normal) EOI */
#define ICW4_BUF_SLAVE	0x08		/* Buffered mode/slave */
#define ICW4_BUF_MASTER	0x0C		/* Buffered mode/master */
#define ICW4_SFNM	0x10		/* Special fully nested (not) */

// The IRQ line on the master PIC that the slave PIC is connected to.
#define CASCADE_IRQ 2

// OCW3 commands for reading the IRR and ISR registers.
#define PIC_READ_IRR    0x0a    /* OCW3 irq ready next CMD read */
#define PIC_READ_ISR    0x0b    /* OCW3 irq service next CMD read */

/**
 * @brief Remaps the PIC interrupt vectors.
 * @param offset1 The new starting vector for the master PIC (e.g., 0x20 for IRQ0-7).
 * @param offset2 The new starting vector for the slave PIC (e.g., 0x28 for IRQ8-15).
 */
void pic_remap(int offset1, int offset2)
{
    // Start the initialization sequence in cascade mode.
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
	io_wait();
	outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
	io_wait();

    // ICW2: Set the master PIC's vector offset.
	outb(PIC1_DATA, offset1);
	io_wait();
    // ICW2: Set the slave PIC's vector offset.
	outb(PIC2_DATA, offset2);
	io_wait();

    // ICW3: Tell master PIC that there is a slave PIC at IRQ2 (0000 0100).
	outb(PIC1_DATA, 1 << CASCADE_IRQ);
	io_wait();
    // ICW3: Tell slave PIC its cascade identity (0000 0010).
	outb(PIC2_DATA, CASCADE_IRQ);
	io_wait();
	
    // ICW4: Have the PICs use 8086 mode.
	outb(PIC1_DATA, ICW4_8086);
	io_wait();
	outb(PIC2_DATA, ICW4_8086);
	io_wait();

	// Unmask all interrupts.
	outb(PIC1_DATA, 0);
	outb(PIC2_DATA, 0);
}

void pic_init(void)
{
    // Remap the PIC to a safe offset (32-47), so they don't conflict with CPU exceptions.
    pic_remap(0x20, 0x28);
}

/**
 * @brief Sends an End Of Interrupt signal to the PICs.
 *
 * @param irq The interrupt number that was handled.
 */
void pic_send_eoi(uint8_t irq)
{
    // If the IRQ came from the slave PIC, we need to send an EOI to it.
	if(irq >= 8)
		outb(PIC2_COMMAND, PIC_EOI);
	
    // In either case, we need to send an EOI to the master PIC.
	outb(PIC1_COMMAND, PIC_EOI);
}

/**
 * @brief Masks a given IRQ line, preventing it from triggering interrupts.
 *
 * @param IRQline The IRQ line (0-15) to mask.
 */
void pic_set_mask(uint8_t IRQline)
{
    uint16_t port;
    uint8_t value;

    if(IRQline < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        IRQline -= 8;
    }
    value = inb(port) | (1 << IRQline);
    outb(port, value);      
}

/**
 * @brief Clears the mask on a given IRQ line, allowing it to trigger interrupts.
 *
 * @param IRQline The IRQ line (0-15) to unmask.
 */
void pic_clear_mask(uint8_t IRQline)
{
    uint16_t port;
    uint8_t value;

    if(IRQline < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        IRQline -= 8;
    }
    value = inb(port) & ~(1 << IRQline);
    outb(port, value);   
}

/**
 * @brief Disables the PICs by masking all IRQ lines.
 */
void pic_disabled()
{
    outb(PIC1_DATA, 0xff);
    outb(PIC2_DATA, 0xff);
}

/**
 * @brief Helper function to read a register from both PICs.
 * @param ocw3 The OCW3 command to select the register (PIC_READ_IRR or PIC_READ_ISR).
 */
static uint16_t __pic_get_irq_reg(int ocw3)
{
    /* OCW3 to PIC CMD to get the register values.  PIC2 is chained, and
     * represents IRQs 8-15.  PIC1 is IRQs 0-7, with 2 being the chain */
    outb(PIC1_COMMAND, ocw3);
    outb(PIC2_COMMAND, ocw3);
    return (inb(PIC2_COMMAND) << 8) | inb(PIC1_COMMAND);
}

/* Returns the combined value of the cascaded PICs irq request register */
uint16_t pic_get_irr(void)
{
    return __pic_get_irq_reg(PIC_READ_IRR);
}

/* Returns the combined value of the cascaded PICs in-service register */
uint16_t pic_get_isr(void)
{
    return __pic_get_irq_reg(PIC_READ_ISR);
}