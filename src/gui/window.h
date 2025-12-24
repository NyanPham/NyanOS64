#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    int x;
    int y;
    int width;
    int height;
    char *title;
    bool is_dragging;
} Window;

typedef struct WinDragCtx
{
    Window *target;
    int64_t off_x;
    int64_t off_y;
} WinDragCtx;

void init_window_manager(void);
void update_window_drag(Window *win, int16_t dx, int16_t dy);
bool check_window_drag(Window *win, int64_t mouse_x, int64_t mouse_y);
void stop_window_drag(Window *win);
void window_paint();
void window_update(void);

#endif