#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void timer_init();
uint64_t timer_get_ticks();

#endif