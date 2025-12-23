#include "window.h"
#include "drivers/video.h"
#include "kern_defs.h"
#include "drivers/serial.h" // debugging

#define WIN_W 200
#define WIN_H 150
#define TITLE_H 20

static int64_t win_x = 300;
static int64_t win_y = 200;

static bool is_dragging = false;

/*
We need a buffer to save the background of the whole Window.
Size = 200 * 150 * 4 = 120KB
*/
static uint32_t win_bg[WIN_W * WIN_H];

static void save_win_bg(int64_t x, int64_t y)
{
    for (int r = 0; r < WIN_H; r++)
    {
        for (int c = 0; c < WIN_W; c++)
        {
            win_bg[(r * WIN_W) + c] = video_get_pixel(x + c, y + r);
        }
    }
}

static void restore_win_bg(int64_t x, int64_t y)
{
    for (int r = 0; r < WIN_H; r++)
    {
        for (int c = 0; c < WIN_W; c++)
        {
            video_plot_pixel(x + c, y + r, win_bg[(r * WIN_W) + c]);
        }
    }
}

static void draw_window(int64_t x, int64_t y)
{
    // draw the title bar with blue
    for (int r = 0; r < TITLE_H; r++)
    {
        for (int c = 0; c < WIN_W; c++)
        {
            video_plot_pixel(x + c, y + r, Blue);
        }
    }

    // draw the body of window with slate grey
    for (int r = TITLE_H; r < WIN_H; r++)
    {
        for (int c = 0; c < WIN_W; c++)
        {
            video_plot_pixel(x + c, y + r, Slate);
        }
    }

    // add border
    for (int c = 0; c < WIN_W; c++)
    {
        video_plot_pixel(x + c, y, White);             // border top
        video_plot_pixel(x + c, y + WIN_H - 1, Black); // border bottom
    }
    for (int r = 0; r < WIN_H; r++)
    {
        video_plot_pixel(x, y + r, White);             // border left
        video_plot_pixel(x + WIN_W - 1, y + r, Black); // border right
    }
}

void init_window_manager(void)
{
    save_win_bg(win_x, win_y);
    draw_window(win_x, win_y);
    kprint("Window manager inited!\n");
}

void update_window_drag(int16_t dx, int16_t dy)
{
    if (!is_dragging)
    {
        return;
    }

    restore_win_bg(win_x, win_y);
    win_x += dx;
    win_y -= dy;

    save_win_bg(win_x, win_y);
    draw_window(win_x, win_y);

    // DEBUG:
    // kprint("MOVE WINDOW\n");
    // kprint("win_x: ");
    // kprint_hex_64(win_x);
    // kprint("\n");

    // kprint("win_y: ");
    // kprint_hex_64(win_y);
    // kprint("\n");
}

bool check_window_drag(int64_t mouse_x, int64_t mouse_y)
{
    if (mouse_x >= win_x && mouse_x < win_x + WIN_W && mouse_y >= win_y && mouse_y < win_y + TITLE_H)
    {
        is_dragging = true;
        return true;
    }

    return false;
}

void stop_window_drag(void)
{
    is_dragging = false;

    // DEBUG
    // kprint("STOPPED DRAGGING\n");
    // kprint("win_x: ");
    // kprint_hex_64(win_x);
    // kprint("\n");

    // kprint("win_y: ");
    // kprint_hex_64(win_y);
    // kprint("\n");
}

void window_paint()
{
    draw_window(win_x, win_y);
}