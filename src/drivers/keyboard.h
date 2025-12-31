#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

#define KBD_BUF_SIZE 0x100 // 256

typedef struct
{
    char buf[KBD_BUF_SIZE];
    uint8_t head;
    uint8_t tail;
    int64_t waiting_pid;
} kbd_buf_t;

char keyboard_get_char(void); 
void keyboard_set_waiting(int64_t pid);
void keyboard_init(void);

#endif