#ifndef BCD_H
#define BCD_H

#include <stdint.h>

inline uint8_t bcd2bin(uint8_t bcd)
{
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

#endif