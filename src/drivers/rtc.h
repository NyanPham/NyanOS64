#ifndef RTC_H
#define RTC_H

#include "include/time.h"
#include <stdint.h>

uint8_t rtc_read(uint8_t reg);
Time_t *rtc_get_time(void);

#endif