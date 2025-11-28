#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

#define kbd_buf_SIZE 0x100 // 256

typedef struct
{
    char buf[kbd_buf_SIZE];
    uint8_t head;
    uint8_t tail;
} kbd_buf_t;

char keyboard_get_char(void); 
void keyboard_init(void);

#endif