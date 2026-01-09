#ifndef WINDOW_H
#define WINDOW_H

#include "kern_defs.h"
#include "ansi.h"

#include <stdint.h>
#include <stdbool.h>

#define WIN_W 700
#define WIN_H 350
#define WIN_TITLE_BAR_H 20
#define WIN_BORDER_SIZE 1
#define WIN_MIN_W 70
#define WIN_MIN_H (WIN_TITLE_BAR_H * 2)

// flags
#define WIN_MOVABLE (1 << 0)
#define WIN_RESIZEABLE (1 << 1)
#define WIN_MINIMIZABLE (1 << 2)
#define WIN_DIRTY (1U << 31)

// resize directions
#define RES_NONE 0
#define RES_LEFT (1 << 0)
#define RES_RIGHT (1 << 1)
#define RES_TOP (1 << 2)
#define RES_BOTTOM (1 << 3)
#define WIN_RESIZE_MARGIN 3

typedef struct Window
{
    int64_t x;       // coord x in pixels
    int64_t y;       // coord y in pixels
    uint64_t width;  // width in pixels
    uint64_t height; // height in pixels
    char *title;

    int64_t cursor_x; // Row cell index, not a pixel
    int64_t cursor_y; // Col cell index, not a pixel
    Pixel *pixels;
    uint64_t pixels_size;

    // Doubly Linked List to handle bring to front
    // and back more easily
    struct Window *prev;
    struct Window *next;

    int64_t owner_pid;
    uint32_t flags;

    Rect *clip_list;
} Window;

typedef struct WinDragCtx
{
    Window *target;
    int64_t off_x;
    int64_t off_y;
    uint32_t resize_dir; // 0 is to move the window, otherwise to resize
} WinDragCtx;

void init_win_manager(void);
bool check_win_drag(Window *win, int64_t mouse_x, int64_t mouse_y);
void win_paint(void);
void win_update(void);
Window *win_create(int64_t x, int64_t y, uint64_t width, uint64_t height, const char *title, uint32_t flags);
Window *get_win_at(int64_t mx, int64_t my);
void win_focus(Window *win);
void win_close(Window *win);
void win_put_char(Window *win, char c);
void win_draw_char_at(Window *win, char c, uint64_t x, uint64_t y, GBA_Color fg_color, GBA_Color bg_color);
void win_move(Window *win, int64_t new_x, int64_t new_y);
void win_resize(Window *win, int64_t new_x, int64_t new_y, int64_t new_w, int64_t new_h);
Window *win_get_active(void);
bool is_point_in_rect(int64_t px, int64_t py, int64_t rx, int64_t ry, int64_t rw, int64_t rh);
uint8_t rect_intersect(Rect *r1, Rect *r2);
Rect *clip_rect(Rect *r, Rect *clipper);
void recalc_clip_list(Window *win);

#endif