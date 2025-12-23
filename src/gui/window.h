#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>
#include <stdbool.h>

void init_window_manager(void);
void update_window_drag(int16_t dx, int16_t dy);
bool check_window_drag(int64_t mouse_x, int64_t mouse_y);
void stop_window_drag(void);
void window_paint(void);

#endif