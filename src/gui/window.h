#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Window
{
    int64_t x;
    int64_t y;
    uint64_t width;
    uint64_t height;
    char *title;
    bool is_dragging;

    // Doubly Linked List to handle bring to front
    // and back more easily
    struct Window *prev;
    struct Window *next;
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
void window_paint(void);
void window_update(void);
Window *create_win(int64_t x, int64_t y, uint64_t width, uint64_t height, const char *title);
Window *get_win_at(int64_t mx, int64_t my);
void focus_win(Window *win);
void close_win(Window *win);

#endif