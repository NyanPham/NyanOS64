#include "window.h"
#include "drivers/video.h"
#include "drivers/mouse.h"
#include "mem/kmalloc.h"
#include "kern_defs.h"
#include "drivers/serial.h" // debugging

#include <stddef.h>

#define WIN_W 200
#define WIN_H 150
#define TITLE_H 20

static Window *g_win_list = NULL; // Bottom / Head of lis
static Window *g_win_top = NULL;  // Top / Tail of List to focus

static WinDragCtx drag_ctx =
    {
        .target = NULL,
        .off_x = 0,
        .off_y = 0,
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

Window *create_win(int64_t x, int64_t y, uint64_t width, uint64_t height, const char *title)
{
    Window *win = (Window*)kmalloc(sizeof(Window));
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->title = title;
    win->is_dragging = false;
    win->next = NULL;

    if (g_win_list == NULL || g_win_top == NULL)
    {
        win->prev = NULL;
        g_win_list = win;
        g_win_top = win;
    }
    else
    {
        win->prev = g_win_top;
        g_win_top->next = win;
        g_win_top = win;
    }

    return win;
}

void init_window_manager(void)
{
    Window *win1 = create_win(300, 200, 200, 150, "Test App");
    // Create second window to test
    Window *win2 = create_win(350, 230, 250, 250, "Test App 2");

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
    Window *curr = g_win_list;
    while (curr != NULL)
    {
        draw_window(curr);
        curr = curr->next;
    }
}

Window *get_win_at(int64_t mx, int64_t my)
{
    Window *curr = g_win_top;

    while (curr != NULL)
    {
        if (mx >= curr->x &&
            mx < curr->x + curr->width &&
            my >= curr->y &&
            my < curr->y + curr->height)
        {
            return curr;
        }
        curr = curr->prev;
    }

    return NULL;
}

/**
 * @brief Move the window to front
 */
void focus_win(Window *win)
{
    if (win == g_win_top)
    {
        return;
    }

    if (win == g_win_list)
    {
        g_win_top->next = g_win_list;
        g_win_list->prev = g_win_top;
        g_win_top = g_win_list;
        g_win_list = g_win_list->next;
        g_win_list->prev = NULL;
        g_win_top->next = NULL;
        return;
    }

    // unlink the win
    if (win->prev != NULL && win->next != NULL)
    {
        win->prev->next = win->next;
        win->next->prev = win->prev;
        win->next = NULL;
    }

    // relink to the tail
    win->prev = g_win_top;
    g_win_top->next = win;
    g_win_top = win;
}

void close_win(Window *win)
{
    if (win == drag_ctx.target)
    {
        drag_ctx.target = NULL;
    }

    if (win->prev == NULL && win->next == NULL) // the only one left
    {
        g_win_list = NULL;
        g_win_top = NULL;
        kfree(win);
        return;
    }

    if (win == g_win_list)
    {
        g_win_list->next->prev = NULL;
        g_win_list = g_win_list->next;
        kfree(win);
        return;
    }

    if (win == g_win_top)
    {
        g_win_top->prev->next = NULL;
        g_win_top = g_win_top->prev;
        kfree(win);
        return;
    }

    win->prev->next = win->next;
    win->next->prev = win->prev;
    kfree(win);
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
        else
        {
            Window *curr_win = get_win_at(mx, my);
            if (curr_win != NULL)
            {
                focus_win(curr_win);
                if (check_window_drag(curr_win, mx, my))
                {
                    drag_ctx.target = curr_win;
                    drag_ctx.off_x = mx - curr_win->x;
                    drag_ctx.off_y = my - curr_win->y;
                }
            }
        }
    }
    else
    {
        drag_ctx.target = NULL;
    }
}