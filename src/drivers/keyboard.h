#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

char keyboard_get_char(void); 
void keyboard_set_waiting(int64_t pid);
void keyboard_init(void);
bool keyboard_ctrl_presssed(void); 
bool keyboard_alt_presssed(void); 
bool keyboard_shift_presssed(void); 
bool keyboard_is_caps_lock(void); 

#endif