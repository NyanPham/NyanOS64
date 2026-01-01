#ifndef WINDOW_H
#define WINDOW_H

#include "drivers/video.h"
#include "ansi.h"

#include <stdint.h>
#include <stdbool.h>

#define WIN_W 700
#define WIN_H 350
#define WIN_TITLE_BAR_H 20
#define WIN_BORDER_SIZE 1

typedef struct Window
{
    int64_t x;       // coord x in pixels
    int64_t y;       // coord y in pixels
    uint64_t width;  // width in pixels
    uint64_t height; // height in pixels
    char *title;
    bool is_dragging;

    Pixel *pixels;

    int64_t cursor_x; // Row cell index, not a pixel
    int64_t cursor_y; // Col cell index, not a pixel

    // Doubly Linked List to handle bring to front
    // and back more easily

    struct Window *prev;
    struct Window *next;

    int64_t owner_pid;
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
void win_put_char(Window *win, char c);
void win_draw_char_at(Window *win, char c, uint64_t x, uint64_t y, GBA_Color fg_color, GBA_Color bg_color);
Window* win_get_active(void);

#endif