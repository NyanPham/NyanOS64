#include "window.h"
#include "drivers/video.h"
#include "drivers/mouse.h"
#include "mem/kmalloc.h"
#include "mem/vmm.h"
#include "kern_defs.h"
#include "sched/sched.h"
#include "../string.h"
#include "ansi.h"
#include "event/event.h"
#include "drivers/serial.h" // debugging
#include "kern_defs.h"

#include <stddef.h>

// From font.h
extern char font8x8_basic[128][8];
extern EventBuf g_event_queue;

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
    // Blitting/photocopy from win->pixels to screen
    // draw the body of window with slate grey
    for (int r = 0; r < win->height; r++)
    {
        for (int c = 0; c < win->width; c++)
        {
            Pixel pixel = win->pixels[r * win->width + c];
            int64_t scrn_x = c + win->x;
            int64_t scrn_y = r + win->y;
            video_plot_pixel(scrn_x, scrn_y, pixel.color);
        }
    }
}

/**
 * @brief Inits the title and content background to the back buffer `pixels` of a window
 */
static void init_win_pixels(Window *win)
{
    /* TITLE BAR */
    // init the title bar
    for (int64_t r = 0; r < WIN_TITLE_BAR_H; r++)
    {
        for (int64_t c = 0; c < win->width; c++)
        {
            win->pixels[r * win->width + c].color = Blue;
        }
    }

    // draw the title
    char *title = win->title;
    for (int64_t x = 5; (x < win->width - 15 && *title); x += CHAR_W)
    {
        // y is always 5
        win_draw_char_at(win, *title, x, 5, White, Blue);
        title++;
    }

    // draw the close button
    int64_t btn_size = WIN_TITLE_BAR_H;
    int64_t btn_x = win->width - btn_size;
    int64_t btn_y = 0;

    for (int64_t r = btn_y; r < btn_y + btn_size; r++)
    {
        for (int64_t c = btn_x; c < btn_x + btn_size; c++)
        {
            int64_t rel_x = c - btn_x;
            int64_t rel_y = r - btn_y;
            bool is_cross = (rel_x == rel_y) || (rel_x + rel_y == btn_size - 1);
            GBA_Color color = Red;

            if (is_cross)
            {
                if (rel_x > 2 && rel_x < btn_size - 3)
                {
                    color = White;
                }
            }
            win->pixels[r * win->width + c].color = color;
        }
    }

    /* CONTENT BACKGROUND */
    for (int64_t r = WIN_TITLE_BAR_H; r < win->height; r++)
    {
        for (int64_t c = 0; c < win->width; c++)
        {
            win->pixels[r * win->width + c].color = Slate;
        }
    }

    /* BORDERS */
    for (int c = 0; c < win->width; c++)
    {
        win->pixels[c].color = White;
        win->pixels[c + (win->width * (win->height - 1))].color = Black;
    }
    for (int r = 0; r < win->height; r++)
    {
        win->pixels[r * win->width].color = White;
        win->pixels[(r * win->width) + win->width - 1].color = Black;
    }
}

static uint32_t get_resize_dir(Window *win, int64_t mx, int64_t my)
{
    int64_t r_left = win->x;
    int64_t r_right = win->x + win->width;
    int64_t r_top = win->y;
    int64_t r_bottom = win->y + win->height;

    uint32_t dir = RES_NONE;
    if (mx <= r_left + WIN_RESIZE_MARGIN && mx >= r_left - WIN_RESIZE_MARGIN)
    {
        dir |= RES_LEFT;
    }

    if (mx <= r_right + WIN_RESIZE_MARGIN && mx >= r_right - WIN_RESIZE_MARGIN)
    {
        dir |= RES_RIGHT;
    }

    if (my <= r_top + WIN_RESIZE_MARGIN && my >= r_top - WIN_RESIZE_MARGIN)
    {
        dir |= RES_TOP;
    }

    if (my <= r_bottom + WIN_RESIZE_MARGIN && my >= r_bottom - WIN_RESIZE_MARGIN)
    {
        dir |= RES_BOTTOM;
    }

    return dir;
}

Window *create_win(int64_t x, int64_t y, uint64_t width, uint64_t height, const char *title, uint32_t flags)
{
    Window *win = (Window *)kmalloc(sizeof(Window));
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->title = title;
    win->flags = flags;
    win->cursor_x = 0;
    win->cursor_y = 0;

    // create buf to paint for the window body
    uint64_t pixel_buf_size = height * width * sizeof(Pixel);
    Pixel *pixel_buf = (Pixel *)vmm_alloc(pixel_buf_size);
    if (pixel_buf == NULL)
    {
        kprint("Panic: Failed to kmalloc back buffer for window\n");
        kfree(win);

        return NULL;
    }

    memset(pixel_buf, 0, pixel_buf_size);
    win->pixels = pixel_buf;
    win->pixels_size = pixel_buf_size;
    init_win_pixels(win);

    // link it to the list
    // the latest created win is always
    // on top
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

    Task *tsk = get_curr_task();
    if (tsk == NULL)
    {
        kprint("Alert: A window is created but not attached to any running task!\n");
    }
    else
    {
        tsk->win = win;
        tsk->win->owner_pid = tsk->pid;
    }

    return win;
}

void init_window_manager(void)
{
    kprint("Window manager inited!\n");
}

bool check_window_drag(Window *win, int64_t mouse_x, int64_t mouse_y)
{
    return (win->flags & WIN_MOVABLE) && is_point_in_rect(mouse_x, mouse_y, win->x, win->y, win->width, WIN_TITLE_BAR_H);
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
        vmm_free(win->pixels, win->height * win->width * sizeof(Pixel));
        kfree(win);
        return;
    }

    if (win == g_win_list)
    {
        g_win_list->next->prev = NULL;
        g_win_list = g_win_list->next;
        vmm_free(win->pixels, win->height * win->width * sizeof(Pixel));
        kfree(win);
        return;
    }

    if (win == g_win_top)
    {
        g_win_top->prev->next = NULL;
        g_win_top = g_win_top->prev;
        vmm_free(win->pixels, win->height * win->width * sizeof(Pixel));
        kfree(win);
        return;
    }

    win->prev->next = win->next;
    win->next->prev = win->prev;
    vmm_free(win->pixels, win->height * win->width * sizeof(Pixel));
    kfree(win);
}

void win_draw_char_at(Window *win, char c, uint64_t x, uint64_t y, GBA_Color fg_color, GBA_Color bg_color)
{
    char *glyph = font8x8_basic[(int)c];
    for (uint8_t dy = 0; dy < CHAR_H; dy++)
    {
        for (uint8_t dx = 0; dx < CHAR_W; dx++)
        {
            uint64_t idx = (y + dy) * win->width + (x + dx);

            if ((glyph[dy] >> dx) & 1)
            {
                win->pixels[idx].color = (uint32_t)fg_color;
            }
            else
            {
                win->pixels[idx].color = (uint32_t)bg_color;
            }
        }
    }
}

void win_put_char(Window *win, char c)
{
    if (c == '\n')
    {
        win->cursor_x = 0;
        win->cursor_y += CHAR_H;
    }
    else
    {
        int64_t scrn_x = win->x + WIN_BORDER_SIZE + win->cursor_x;
        // int64_t scrn_y = win->y + WIN_TITLE_BAR_H + win->cursor_y;

        if (scrn_x >= win->x + win->width - WIN_BORDER_SIZE)
        {
            win->cursor_x = 0;
            win->cursor_y += CHAR_H;

            scrn_x = win->x + WIN_BORDER_SIZE + win->cursor_x;
            // scrn_y = win->y + WIN_TITLE_BAR_H + win->cursor_y;
        }

        win_draw_char_at(win, c, win->cursor_x + WIN_BORDER_SIZE, win->cursor_y + WIN_TITLE_BAR_H, Black, Slate);
        win->cursor_x += CHAR_W;
    }
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
            if (drag_ctx.resize_dir != RES_NONE)
            {
                int64_t new_x = drag_ctx.target->x;
                int64_t new_y = drag_ctx.target->y;
                int64_t new_w = drag_ctx.target->width;
                int64_t new_h = drag_ctx.target->height;
                uint8_t changed = 0;

                if (drag_ctx.resize_dir & RES_LEFT)
                {
                    int64_t tmp_w = drag_ctx.target->x + drag_ctx.target->width - mx;
                    if (tmp_w >= WIN_MIN_W && tmp_w != new_w)
                    {
                        new_w = tmp_w;
                        new_x = mx;
                        changed = 1;
                    }
                }
                else if (drag_ctx.resize_dir & RES_RIGHT)
                {
                    int64_t tmp_w = mx - drag_ctx.target->x;
                    if (tmp_w >= WIN_MIN_W && tmp_w != new_w)
                    {
                        new_w = tmp_w;
                        changed = 1;
                    }
                }

                if (drag_ctx.resize_dir & RES_TOP)
                {
                    int64_t tmp_h = drag_ctx.target->y + drag_ctx.target->height - my;
                    if (tmp_h >= WIN_MIN_H && tmp_h != new_h)
                    {
                        new_h = tmp_h;
                        new_y = my;
                        changed = 1;
                    }
                }
                else if (drag_ctx.resize_dir & RES_BOTTOM)
                {
                    int64_t tmp_h = my - drag_ctx.target->y;
                    if (tmp_h >= WIN_MIN_H && tmp_h != new_h)
                    {
                        new_h = tmp_h;
                        changed = 1;
                    }
                }

                if (changed)
                {
                    int64_t new_pixels_size = new_h * new_w * sizeof(Pixel);
                    Pixel *new_pixels = (Pixel *)vmm_realloc(
                        drag_ctx.target->pixels,
                        drag_ctx.target->pixels_size,
                        new_pixels_size);

                    if (new_pixels != NULL)
                    {
                        drag_ctx.target->pixels = new_pixels;
                        drag_ctx.target->pixels_size = new_pixels_size;
                        drag_ctx.target->x = new_x;
                        drag_ctx.target->y = new_y;
                        drag_ctx.target->width = new_w;
                        drag_ctx.target->height = new_h;
                    }
                }
            }
            else
            {
                drag_ctx.target->x = mx - drag_ctx.off_x;
                drag_ctx.target->y = my - drag_ctx.off_y;
            }
        }
        else
        {
            Window *curr_win = get_win_at(mx, my);
            if (curr_win != NULL)
            {
                focus_win(curr_win);
                int64_t off_mx = mx - curr_win->x;
                int64_t off_my = my - curr_win->y;
                uint32_t res_size = get_resize_dir(curr_win, mx, my);

                if (res_size != RES_NONE || check_window_drag(curr_win, mx, my))
                {
                    drag_ctx.target = curr_win;
                    drag_ctx.off_x = off_mx;
                    drag_ctx.off_y = off_my;
                    drag_ctx.resize_dir = res_size;
                }

                int64_t btn_size = WIN_TITLE_BAR_H;
                int64_t btn_x = curr_win->width - btn_size;
                int64_t btn_y = 0;

                if (is_point_in_rect(off_mx, off_my, btn_x, btn_y, btn_size, btn_size))
                {
                    sched_kill(curr_win->owner_pid);
                }
            }
        }
    }
    else
    {
        if (drag_ctx.target != NULL && drag_ctx.resize_dir != RES_NONE)
        {
            Event e = {
                .type = EVENT_WIN_RESIZE,
                .resize_event = {
                    .win_owner_pid = drag_ctx.target->owner_pid,
                },
            };
            event_queue_push(&g_event_queue, e);
            init_win_pixels(drag_ctx.target);
        }
        drag_ctx.target = NULL;
    }
}

Window *win_get_active()
{
    return g_win_top;
}

bool is_point_in_rect(int64_t px, int64_t py, int64_t rx, int64_t ry, int64_t rw, int64_t rh)
{
    return (
        px >= rx && px < rx + rw &&
        py >= ry && py < ry + rh);
}