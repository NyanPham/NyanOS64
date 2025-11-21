#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

#define KB_BUF_SIZE 256

typedef struct
{
    char buf[KB_BUF_SIZE];
    uint8_t head;
    uint8_t tail;
} ring_buf;

char keyboard_get_char(void); 
void keyboard_init(void);

#endif