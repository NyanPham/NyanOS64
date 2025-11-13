#include "serial.h"
#include "../io.h"

#include <stdbool.h>
#include <stddef.h>

// We'll use polling instead of interrupt
// to check if the port for serial is ready.
// Why? It's simple, and its purpose is mainly
// to debug other complicated interrupt-driven drivers. 

static bool g_serial_inited = false;

#define COM1_DATA_PORT 0x3F8
#define COM1_ENBL_INT (COM1_DATA_PORT+1)
#define COM1_FIFO_CTRL (COM1_DATA_PORT+2)
#define COM1_LINE_CTRL (COM1_DATA_PORT+3)
#define COM1_LINE_STAT (COM1_DATA_PORT+5)

bool is_transmit_empty()
{
    return inb(COM1_LINE_STAT) & (1 << 5);
}

void serial_write(char c)
{
    if (!g_serial_inited)
    {
        return;
    }

    while (!is_transmit_empty())
    {
        continue;
    }

    outb(COM1_DATA_PORT, c);
}

void kprint(const char* str)
{
    if (!g_serial_inited)
    {
        return;
    }

    while (*str != '\0')
    {
        serial_write(*str);
        str++;
    }
}

void serial_init()
{
    outb(COM1_ENBL_INT, 0x00); // disable interrupts

    // COM1_DATA_PORT turns into Divisor Latch LSB
    // and COM1_ENBL_INT becomes Divisor Latch MSB
    outb(COM1_LINE_CTRL, 0x80);

    // divisor is 1 for the baud rate of 115200.
    // in 16-bit, it's 0x0001 -> MSB = 0x00, LSB = 0x01
    outb(COM1_DATA_PORT, 0x01);
    outb(COM1_ENBL_INT, 0x00);

    // restore the functions of COM1_DATA_PORT and COM1_ENBL_INT
    // at the same time, we use the "8N1" data format.
    // "8N1" means: 8 data bits, No parity, 1 stop bit
    outb(COM1_LINE_CTRL, 0x03);

    // let's activate the FIFO buffer 
    // which is to flush 16 bytes to wait for
    // the port to write
    outb(COM1_FIFO_CTRL, 0xC7);

    g_serial_inited = true;
}