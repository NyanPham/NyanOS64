#include "window.h"
#include "drivers/video.h"
#include "drivers/mouse.h"
#include "mem/kmalloc.h"
#include "mem/vmm.h"
#include "kern_defs.h"
#include "sched/sched.h"
#include "../string.h"
#include "ansi.h"
#include "drivers/serial.h" // debugging

#include <stddef.h>

#define WIN_W 700
#define WIN_H 350
#define TITLE_BAR_H 20
#define BORDER_SIZE 1
#define CHAR_W 8 // based on the font.h
#define CHAR_H 8

// From font.h
extern char font8x8_basic[128][8];

static Window *g_win_list = NULL; // Bottom / Head of lis
static Window *g_win_top = NULL;  // Top / Tail of List to focus

static WinDragCtx drag_ctx =
    {
        .target = NULL,
        .off_x = 0,
        .off_y = 0,
};

static void win_draw_char_at(Window *win, char c, uint64_t x, uint64_t y, GBA_Color color);

/* START: ANSI DRIVER IMPLEMENTATION FOR WINDOW */

static void win_driver_put_char(void *data, char c)
{
    Window *win = (Window *)data;
    if (c == '\n')
    {
        win->cursor_x = 0;
        win->cursor_y += CHAR_H;
    }
    else if (c == '\b')
    {
        if (win->cursor_x >= CHAR_W)
        {
            win->cursor_x -= CHAR_W;
            uint64_t start_x = win->cursor_x + BORDER_SIZE;
            uint64_t start_y = win->cursor_y + TITLE_BAR_H;

            for (uint64_t dy = 0; dy < CHAR_H; dy++)
            {
                for (uint64_t dx = 0; dx < CHAR_W; dx++)
                {
                    uint64_t idx = (start_y + dy) * win->width + (start_x + dx);
                    win->pixels[idx].color = Slate;
                }
            }
        }
    }
    else
    {
        int64_t scrn_x = win->x + BORDER_SIZE + win->cursor_x;
        // int64_t scrn_y = win->y + TITLE_BAR_H + win->cursor_y;

        if (scrn_x >= win->x + win->width - BORDER_SIZE)
        {
            win->cursor_x = 0;
            win->cursor_y += CHAR_H;

            scrn_x = win->x + BORDER_SIZE + win->cursor_x;
            // scrn_y = win->y + TITLE_BAR_H + win->cursor_y;
        }

        win_draw_char_at(win, c, win->cursor_x + BORDER_SIZE, win->cursor_y + TITLE_BAR_H, win->ansi_ctx.color);
        win->cursor_x += CHAR_W;
    }
}

static void win_driver_set_color(void *data, uint32_t color)
{
    // already done internally in the ansi.c
    (void)data;
    (void)color;
}

static void win_driver_set_cursor(void *data, int x, int y)
{
    // NOTE: x and y from the ansi to represent a cell of character
    // But in window.c, x and y represent the pixel points.
    // So we need to convert char position to pixel position.
    Window *win = (Window *)data;
    win->cursor_x = x * CHAR_W;
    win->cursor_y = y * CHAR_H;

    // check boundary
    if (win->cursor_x >= win->width - BORDER_SIZE)
    {
        win->cursor_x = win->width - CHAR_W - BORDER_SIZE;
    }
    if (win->cursor_y >= win->height - TITLE_BAR_H)
    {
        win->cursor_y = win->height - CHAR_H - TITLE_BAR_H;
    }
}

static void win_driver_clear(void *data, int mode)
{
    Window *win = (Window *)data;
    if (mode == 2)
    {
        win->cursor_x = 0;
        win->cursor_y = 0;

        // repaint the color for the whole terminal
        for (uint64_t r = TITLE_BAR_H; r < win->height; r++)
        {
            for (uint64_t c = BORDER_SIZE; c < win->width - BORDER_SIZE; c++)
            {
                win->pixels[r * win->width + c].color = Slate;
            }
        }
    }
}

static const AnsiDriver g_win_driver = {
    .put_char = win_driver_put_char,
    .set_color = win_driver_set_color,
    .set_cursor = win_driver_set_cursor,
    .clear_screen = win_driver_clear,
    .scroll = NULL,
};

/* END: ANSI DRIVER IMPLEMENTATION FOR WINDOW */

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
    for (uint64_t r = 0; r < TITLE_BAR_H; r++)
    {
        for (uint64_t c = 0; c < win->width; c++)
        {
            win->pixels[r * win->width + c].color = Blue;
        }
    }

    // draw the title
    char *title = win->title;
    for (uint64_t x = 5; (x < win->width - 15 && *title); x += CHAR_W)
    {
        // y is always 5
        win_draw_char_at(win, *title, x, 5, Black);
        title++;
    }

    /* CONTENT BACKGROUND */
    for (uint64_t r = TITLE_BAR_H; r < win->height; r++)
    {
        for (uint64_t c = 0; c < win->width; c++)
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

Window *create_win(int64_t x, int64_t y, uint64_t width, uint64_t height, const char *title)
{
    Window *win = (Window *)kmalloc(sizeof(Window));
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->title = title;
    win->is_dragging = false;
    win->cursor_x = 0;
    win->cursor_y = 0;
    win->ansi_ctx.color = Black;
    win->ansi_ctx.state = ANSI_NORMAL;
    win->ansi_ctx.idx = 0;
    memset(win->ansi_ctx.buf, 0, ANSI_BUF_SIZE);

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
    }

    return win;
}

void init_window_manager(void)
{
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
    if (mouse_x >= win->x && mouse_x < win->x + WIN_W && mouse_y >= win->y && mouse_y < win->y + TITLE_BAR_H)
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

static void win_draw_char_at(Window *win, char c, uint64_t x, uint64_t y, GBA_Color color)
{
    char *glyph = font8x8_basic[(int)c];
    for (uint8_t dy = 0; dy < CHAR_H; dy++)
    {
        for (uint8_t dx = 0; dx < CHAR_W; dx++)
        {
            if ((glyph[dy] >> dx) & 1)
            {
                uint64_t idx = (y + dy) * win->width + (x + dx);
                win->pixels[idx].color = (uint32_t)color;
            }
        }
    }
}

void win_put_char(Window *win, char c)
{
    ansi_write_char(&win->ansi_ctx, c, &g_win_driver, (void *)win);
}

void win_handle_key(char c)
{
    if (c == 0 || g_win_top == NULL)
    {
        return;
    }

    win_put_char(g_win_top, c);
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