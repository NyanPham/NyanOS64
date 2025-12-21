#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

void mouse_init(void);
int64_t mouse_get_x(void);
int64_t mouse_get_y(void);

#endif