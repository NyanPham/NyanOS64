#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

typedef struct Mouse
{
    uint8_t cycle; // (0: stat , 1: x, 2: y)
    union
    {
        uint8_t byte[3]; // (0: stat , 1: x, 2: y)
        struct
        {
            uint8_t stat;
            uint8_t dx;
            uint8_t dy;
        };
    };
    int64_t x;
    int64_t y;
    uint64_t w;
    uint64_t h;
} Mouse;

void mouse_init(void);
int64_t mouse_get_x(void);
int64_t mouse_get_y(void);
int64_t mouse_get_stat(void);

#endif