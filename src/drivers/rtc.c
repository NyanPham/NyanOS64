#include "rtc.h"
#include "../io.h"
#include "mem/kmalloc.h"
#include "utils/asm_instrs.h"
#include "utils/bcd.h"

uint8_t rtc_read(uint8_t reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

Time_t *rtc_get_time()
{
    Time_t *time = (Time_t *)kmalloc(sizeof(Time_t));
    if (time == NULL)
    {
        return NULL;
    }

    time->secs = bcd2bin(rtc_read(0x00));
    time->mins = bcd2bin(rtc_read(0x02));
    time->hrs = bcd2bin(rtc_read(0x04));

    return time;
}