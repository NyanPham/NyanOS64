#ifndef RTC_H
#define RTC_H

#include <stdint.h>

typedef struct
{
    uint8_t secs;
    uint8_t mins;
    uint8_t hrs;
} Time_t;

uint8_t rtc_read(uint8_t reg);
Time_t *rtc_get_time(void);

#endif