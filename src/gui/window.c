#include "window.h"
#include "drivers/video.h"
#include "drivers/mouse.h"
#include "kern_defs.h"
#include "drivers/serial.h" // debugging

#include <stddef.h>

#define WIN_W 200
#define WIN_H 150
#define TITLE_H 20

static WinDragCtx drag_ctx =
    {
        .target = NULL,
        .off_x = 0,
        .off_y = 0,
};

// test instance of window
static Window win_test =
    {
        .x = 300,
        .y = 200,
        .width = 200,
        .height = 150,
        .title = "Test App",
        .is_dragging = false,
};

static void draw_window(Window *win)
{
    // draw the title bar with blue
    for (int r = 0; r < TITLE_H; r++)
    {
        for (int c = 0; c < WIN_W; c++)
        {
            video_plot_pixel(win->x + c, win->y + r, Blue);
        }
    }

    // draw the body of window with slate grey
    for (int r = TITLE_H; r < WIN_H; r++)
    {
        for (int c = 0; c < WIN_W; c++)
        {
            video_plot_pixel(win->x + c, win->y + r, Slate);
        }
    }

    // add border
    for (int c = 0; c < WIN_W; c++)
    {
        video_plot_pixel(win->x + c, win->y, White);             // border top
        video_plot_pixel(win->x + c, win->y + WIN_H - 1, Black); // border bottom
    }
    for (int r = 0; r < WIN_H; r++)
    {
        video_plot_pixel(win->x, win->y + r, White);             // border left
        video_plot_pixel(win->x + WIN_W - 1, win->y + r, Black); // border right
    }
}

void init_window_manager(void)
{
    draw_window(&win_test);
    kprint("Window manager inited!\n");
}

void update_window_drag(Window *win, int16_t dx, int16_t dy)
{
    if (win->is_dragging)
    {
        win->x += dx;
        win->y -= dy;
    }
}

bool check_window_drag(Window *win, int64_t mouse_x, int64_t mouse_y)
{
    if (mouse_x >= win->x && mouse_x < win->x + WIN_W && mouse_y >= win->y && mouse_y < win->y + TITLE_H)
    {
        win->is_dragging = true;
        return true;
    }

    return false;
}

void stop_window_drag(Window *win)
{
    win->is_dragging = false;
}

void window_paint()
{
    draw_window(&win_test);
}

void window_update(void)
{
    int64_t mstat = mouse_get_stat();
    int64_t mx = mouse_get_x();
    int64_t my = mouse_get_y();

    bool is_left_btn = (mstat & 0x01);
    if (is_left_btn)
    {
        if (drag_ctx.target != NULL)
        {
            drag_ctx.target->x = mx - drag_ctx.off_x;
            drag_ctx.target->y = my - drag_ctx.off_y;
        }
        else if (check_window_drag(&win_test, mx, my))
        {
            drag_ctx.target = &win_test;
            drag_ctx.off_x = mx - win_test.x;
            drag_ctx.off_y = my - win_test.y;
        }
    }
    else
    {
        drag_ctx.target = NULL;
    }
}