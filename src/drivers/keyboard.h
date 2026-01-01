#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

char keyboard_get_char(void); 
void keyboard_set_waiting(int64_t pid);
void keyboard_init(void);

#endif