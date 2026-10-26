#include "window.h"
#include "drivers/video.h"
#include "drivers/mouse.h"
#include "drivers/keyboard.h"
#include "mem/kmalloc.h"
#include "mem/vmm.h"
#include "kern_defs.h"
#include "sched/sched.h"
#include "../string.h"
#include "include/ansi.h"
#include "event/event.h"
#include "drivers/serial.h"
#include "cursor.h"
#include "utils/asm_instrs.h"

#include <stddef.h>

extern char font8x8_basic[128][8];
extern EventBuf g_event_queue;
extern void sched_acquire_lock(uint64_t *rflags);
extern void sched_release_lock(uint64_t *rflags);
extern Task *sched_find_task_locked(int pid);

static Window *g_win_list = NULL;
static Window *g_win_top = NULL;
Window *g_desktop_win = NULL;
static int g_pending_launch = LAUNCH_NONE;

#define MAX_RECT_POOL 2000

static Rect g_rect_storage[MAX_RECT_POOL];
static Rect *g_rect_free_list = NULL;

static void init_rect_pool(void);
Rect *rect_alloc(void);
void rect_free(Rect *r);

static WinDragCtx drag_ctx =
    {
        .target = NULL,
        .off_x = 0,
        .off_y = 0,
};

static int win_toggle_maximize(Window *win);
static int win_toggle_minimize(Window *win);
static void win_redraw_titlebar(Window *win);

static void win_stain_list(Window *win)
{
    while (win != NULL)
    {
        win->flags |= WIN_DIRTY;
        win = win->next;
    }
}

static void win_send_event_to_owner(int64_t pid, Event e)
{
    if (pid <= 0)
    {
        return;
    }
    uint64_t rflags;
    sched_acquire_lock(&rflags);
    Task *tsk = sched_find_task_locked((int)pid);
    if (tsk != NULL && tsk->state != TASK_DEAD && tsk->state != TASK_ZOMBIE && tsk->event_queue != NULL)
    {
        event_queue_push(tsk->event_queue, e);
        if (tsk->state == TASK_SLEEPING || tsk->state == TASK_WAITING)
        {
            tsk->state = TASK_READY;
        }
    }
    sched_release_lock(&rflags);
}

static void win_draw(Window *win)
{
    Rect *rect = win->clip_list;
    if (rect == NULL)
    {
        return;
    }

    while (rect != NULL)
    {
        int64_t start_off_x = rect->x - win->x;
        int64_t start_off_y = rect->y - win->y;
        for (uint64_t r = 0; r < rect->h; r++)
        {
            uint32_t *row = (uint32_t *)&win->pixels[(r + start_off_y) * win->width + (start_off_x)];
            int64_t scrn_x = rect->x;
            int64_t scrn_y = r + rect->y;
            video_draw_pixel_line(scrn_x, scrn_y, row, rect->w * sizeof(Pixel));
        }
        video_add_dirty_rect(rect->x, rect->y, rect->w, rect->h);
        rect = rect->next;
    }
}

static void win_redraw_titlebar(Window *win)
{
    if (win == NULL || (win->flags & WIN_BORDERLESS))
    {
        return;
    }

    GBA_Color bar_color = (win == g_win_top) ? Blue : Navy;

    for (uint64_t r = 0; r < WIN_TITLE_BAR_HEIGHT; r++)
    {
        for (uint64_t c = 0; c < win->width; c++)
        {
            win->pixels[r * win->width + c].color = bar_color;
        }
    }

    if (win->title != NULL)
    {
        char *title = win->title;
        int btn_count = 1;
        if (win->flags & WIN_RESIZABLE)
            btn_count++;
        if (win->flags & WIN_MINIMIZABLE)
            btn_count++;
        uint64_t max_title_x = (win->width > (uint64_t)(btn_count * WIN_TITLE_BAR_HEIGHT + 12))
                                   ? (win->width - (btn_count * WIN_TITLE_BAR_HEIGHT + 12))
                                   : 5;

        for (uint64_t x = 5; (x < max_title_x && *title); x += CHAR_W)
        {
            win_draw_char_at(win, *title, x, 5, White, bar_color);
            title++;
        }
    }

    int btn_size = WIN_TITLE_BAR_HEIGHT;

    if (win->flags & WIN_MINIMIZABLE)
    {
        int min_btn_x = (int)win->width - (3 * btn_size);
        int min_btn_y = 0;
        int left_margin = 4;
        int right_margin = btn_size - 4;
        int line_y = btn_size - 5;

        for (int r = min_btn_y; r < min_btn_y + btn_size; r++)
        {
            int rel_y = r - min_btn_y;
            for (int c = min_btn_x; c < min_btn_x + btn_size; c++)
            {
                int rel_x = c - min_btn_x;
                uint8_t is_line = (rel_y == line_y && rel_x >= left_margin && rel_x < right_margin) ? 1 : 0;
                win->pixels[r * win->width + c].color = is_line ? Black : Yellow;
            }
        }
    }

    if (win->flags & WIN_RESIZABLE)
    {
        int max_btn_x = (int)win->width - (2 * btn_size);
        int max_btn_y = 0;
        int left_margin = 5;
        int right_margin = btn_size - 5;
        int top_margin = 5;
        int bot_margin = btn_size - 5;

        for (int64_t r = max_btn_y; r < max_btn_y + btn_size; r++)
        {
            int64_t rel_y = r - max_btn_y;
            for (int64_t c = max_btn_x; c < max_btn_x + btn_size; c++)
            {
                int64_t rel_x = c - max_btn_x;
                uint8_t is_square_border = ((rel_y == top_margin || rel_y == bot_margin) && rel_x >= left_margin && rel_x < right_margin) ||
                                           ((rel_x == left_margin || rel_x == right_margin - 1) && (rel_y >= top_margin && rel_y < bot_margin));
                win->pixels[r * win->width + c].color = is_square_border ? Black : Green;
            }
        }
    }

    int64_t close_btn_x = win->width - btn_size;
    int64_t close_btn_y = 0;

    for (int64_t r = close_btn_y; r < close_btn_y + btn_size; r++)
    {
        for (int64_t c = close_btn_x; c < close_btn_x + btn_size; c++)
        {
            int64_t rel_x = c - close_btn_x;
            int64_t rel_y = r - close_btn_y;
            bool is_cross = (rel_x == rel_y) || (rel_x + rel_y == btn_size - 1);
            GBA_Color color = Red;

            if (is_cross && rel_x > 2 && rel_x < btn_size - 3)
            {
                color = White;
            }
            win->pixels[r * win->width + c].color = color;
        }
    }

    for (uint64_t c = 0; c < win->width; c++)
    {
        win->pixels[c].color = White;
        win->pixels[c + (win->width * (win->height - 1))].color = White;
    }
    for (uint64_t r = 0; r < win->height; r++)
    {
        win->pixels[r * win->width].color = White;
        win->pixels[(r * win->width) + win->width - 1].color = White;
    }

    win->flags |= WIN_DIRTY;
    video_add_dirty_rect(win->x, win->y, win->width, WIN_TITLE_BAR_HEIGHT);
}

static void init_win_pixels(Window *win)
{
    int64_t start_r = (win->flags & WIN_BORDERLESS) ? 0 : WIN_TITLE_BAR_HEIGHT;
    for (uint64_t r = start_r; r < win->height; r++)
    {
        for (uint64_t c = 0; c < win->width; c++)
        {
            win->pixels[r * win->width + c].color = Slate;
        }
    }

    if (!(win->flags & WIN_BORDERLESS))
    {
        win_redraw_titlebar(win);
    }
}

static void desktop_fill_rect_raw(int x, int y, int w, int h, uint32_t color)
{
    if (g_desktop_win == NULL)
        return;
    int max_w = (int)g_desktop_win->width;
    int max_h = (int)g_desktop_win->height;
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x + w > max_w)
        w = max_w - x;
    if (y + h > max_h)
        h = max_h - y;
    if (w <= 0 || h <= 0)
        return;

    for (int r = 0; r < h; r++)
    {
        uint64_t idx = (uint64_t)(y + r) * g_desktop_win->width + (uint64_t)x;
        memset32_sse(&g_desktop_win->pixels[idx], color, (size_t)w);
    }
}

static void draw_taskbar_button(int x, int y, int w, int h, const char *label, uint32_t bg, uint32_t fg, uint32_t border)
{
    desktop_fill_rect_raw(x, y, w, h, border);
    desktop_fill_rect_raw(x + 1, y + 1, w - 2, h - 2, bg);

    int max_chars = (w - 8) / CHAR_W;
    char truncated[32];
    int i = 0;
    while (label && label[i] && i < max_chars && i < 31)
    {
        truncated[i] = label[i];
        i++;
    }
    truncated[i] = '\0';

    int text_x = x + 5;
    int text_y = y + (h - CHAR_H) / 2;
    win_draw_string(g_desktop_win, text_x, text_y, truncated, fg, bg);
}

void win_draw_taskbar(void)
{
    if (g_desktop_win == NULL)
    {
        return;
    }

    int scrn_w = (int)g_desktop_win->width;
    int scrn_h = (int)g_desktop_win->height;
    int bar_y = scrn_h - TASKBAR_HEIGHT;

    // Taskbar background + top border line
    desktop_fill_rect_raw(0, bar_y, scrn_w, TASKBAR_HEIGHT, 0xFF1A2228);
    desktop_fill_rect_raw(0, bar_y, scrn_w, 1, White);

    int btn_y = bar_y + 4;
    int btn_h = TASKBAR_HEIGHT - 8;

    // Quick-Launch Buttons
    draw_taskbar_button(4, btn_y, 62, btn_h, "+Term", Green, Black, White);
    draw_taskbar_button(70, btn_y, 62, btn_h, "Snake", Orange, Black, White);
    draw_taskbar_button(136, btn_y, 62, btn_h, "Clock", Cyan, Black, White);

    // Separator
    desktop_fill_rect_raw(204, bar_y + 4, 2, btn_h, Gray);

    // Open Window Buttons
    int curr_x = 212;
    int max_x = scrn_w - 84;
    Window *w = g_win_list;
    while (w != NULL)
    {
        if (!(w->flags & WIN_BORDERLESS))
        {
            if (curr_x + 104 <= max_x)
            {
                bool is_focused = (w == g_win_top) && (w->state != WIN_STATE_MINIMIZED);
                uint32_t bg = is_focused ? Blue : (w->state == WIN_STATE_MINIMIZED ? 0xFF282828 : Slate);
                uint32_t fg = (w->state == WIN_STATE_MINIMIZED) ? Yellow : White;
                uint32_t border = is_focused ? Yellow : LightGray;
                const char *title = (w->title != NULL) ? w->title : "Window";
                draw_taskbar_button(curr_x, btn_y, 100, btn_h, title, bg, fg, border);
                curr_x += 104;
            }
        }
        w = w->next;
    }

    g_desktop_win->flags |= WIN_DIRTY;
    video_add_dirty_rect(0, bar_y, scrn_w, TASKBAR_HEIGHT);
}

int win_consume_launch_req(void)
{
    int req = g_pending_launch;
    g_pending_launch = LAUNCH_NONE;
    return req;
}

static uint32_t get_resize_dir(Window *win, int64_t mx, int64_t my)
{
    if (!(win->flags & WIN_RESIZABLE) || win->state == WIN_STATE_MINIMIZED)
    {
        return RES_NONE;
    }

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

Window *win_create(int64_t x, int64_t y, uint64_t width, uint64_t height, const char *title, uint32_t flags)
{
    Window *win = (Window *)kmalloc(sizeof(Window));
    if (win == NULL)
    {
        return NULL;
    }

    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->title = (char *)title;
    win->flags = flags;
    win->cursor_x = 0;
    win->cursor_y = 0;
    win->state = WIN_STATE_NORMAL;
    win->pre_state = WIN_STATE_NORMAL;
    win->pre_x = x;
    win->pre_y = y;
    win->pre_w = width;
    win->pre_h = height;
    win->owner_pid = -1;

    if (!(flags & WIN_BORDERLESS))
    {
        win->width += WIN_BORDER_SIZE * 2;
        win->height += WIN_TITLE_BAR_HEIGHT + WIN_BORDER_SIZE * 2;
    }

    uint64_t pixel_buf_size = win->height * win->width * sizeof(Pixel);
    Pixel *pixel_buf = (Pixel *)vmm_alloc_global(pixel_buf_size);
    if (pixel_buf == NULL)
    {
        kprint("Panic: Failed to kmalloc back buffer for window\n");
        kfree(win);
        return NULL;
    }

    memset_sse(pixel_buf, 0, pixel_buf_size);
    win->pixels = pixel_buf;
    win->pixels_size = pixel_buf_size;

    uint64_t rflags = read_rflags();
    cli();
    Window *old_top = g_win_top;
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
    if (rflags & (1 << 9))
    {
        sti();
    }

    init_win_pixels(win);
    if (old_top != NULL && !(old_top->flags & WIN_BORDERLESS))
    {
        win_redraw_titlebar(old_top);
    }

    Task *tsk = get_curr_task();
    if (tsk != NULL && !(flags & WIN_BORDERLESS))
    {
        tsk->win = win;
        win->owner_pid = tsk->pid;
    }

    Rect *rect = rect_alloc();
    if (rect == NULL)
    {
        kprint("Panic: Out of memory to create rect in win_create!\n");
        if (win->pixels != NULL)
        {
            vmm_free(win->pixels);
        }
        kfree(win);
        return NULL;
    }
    rect->x = win->x;
    rect->y = win->y;
    rect->w = win->width;
    rect->h = win->height;
    rect->next = NULL;

    win->clip_list = rect;
    win->flags |= WIN_DIRTY;

    if (!(flags & WIN_BORDERLESS))
    {
        win_draw_taskbar();
    }

    return win;
}

void init_win_manager(void)
{
    init_rect_pool();
    init_desktop();
    win_paint();
    kprint("Window manager inited!\n");
}

bool check_win_drag(Window *win, int64_t mouse_x, int64_t mouse_y)
{
    return (win->flags & WIN_MOVABLE) && is_point_in_rect(mouse_x, mouse_y, win->x, win->y, win->width, WIN_TITLE_BAR_HEIGHT);
}

void win_paint(void)
{
    uint64_t rflags = read_rflags();
    cli();
    Window *curr = g_win_list;
    while (curr != NULL)
    {
        if (curr->flags & WIN_DIRTY)
        {
            recalc_clip_list(curr);
            win_draw(curr);
            curr->flags &= ~WIN_DIRTY;
        }
        curr = curr->next;
    }
    if (rflags & (1 << 9))
    {
        sti();
    }
}

Window *get_win_at(int64_t mx, int64_t my)
{
    if (g_desktop_win != NULL && my >= (int64_t)video_get_height() - TASKBAR_HEIGHT)
    {
        return g_desktop_win;
    }

    Window *curr = g_win_top;
    while (curr != NULL)
    {
        if (mx >= curr->x &&
            mx < curr->x + (int64_t)curr->width &&
            my >= curr->y &&
            my < curr->y + (int64_t)curr->height)
        {
            return curr;
        }
        curr = curr->prev;
    }

    return NULL;
}

void win_focus(Window *win)
{
    if (win == NULL || (win->flags & WIN_BORDERLESS) || (win == g_win_top))
    {
        return;
    }

    Window *old_top = g_win_top;

    if (win == g_win_list)
    {
        g_win_top->next = g_win_list;
        g_win_list->prev = g_win_top;
        g_win_top = g_win_list;
        g_win_list = g_win_list->next;
        g_win_list->prev = NULL;
        g_win_top->next = NULL;
    }
    else
    {
        if (win->prev != NULL && win->next != NULL)
        {
            win->prev->next = win->next;
            win->next->prev = win->prev;
            win->next = NULL;
        }

        win->prev = g_win_top;
        g_win_top->next = win;
        g_win_top = win;
    }

    if (old_top != NULL && !(old_top->flags & WIN_BORDERLESS))
    {
        win_redraw_titlebar(old_top);
    }
    win_redraw_titlebar(win);
    win_draw_taskbar();
    win_stain_list(g_win_list);
    video_add_dirty_rect(win->x, win->y, win->width, win->height);
}

void win_cycle_focus(void)
{
    if (g_win_list == NULL)
    {
        return;
    }

    // Find the lowest non-borderless window in z-order and bring it to front
    Window *curr = g_win_list;
    while (curr != NULL)
    {
        if (!(curr->flags & WIN_BORDERLESS) && curr != g_win_top)
        {
            if (curr->state == WIN_STATE_MINIMIZED)
            {
                win_toggle_minimize(curr);
            }
            win_focus(curr);
            return;
        }
        curr = curr->next;
    }
}

void win_close(Window *win)
{
    uint64_t rflags = read_rflags();
    cli();
    video_add_dirty_rect(win->x, win->y, win->width, win->height);

    if (win == drag_ctx.target)
    {
        drag_ctx.target = NULL;
    }

    if (win->prev == NULL && win->next == NULL)
    {
        g_win_list = NULL;
        g_win_top = NULL;
    }
    else if (win == g_win_list)
    {
        g_win_list = g_win_list->next;
        if (g_win_list)
        {
            g_win_list->prev = NULL;
        }
    }
    else if (win == g_win_top)
    {
        g_win_top = g_win_top->prev;
        if (g_win_top)
        {
            g_win_top->next = NULL;
        }
    }
    else
    {
        win->prev->next = win->next;
        win->next->prev = win->prev;
    }

    if (rflags & (1 << 9))
    {
        sti();
    }

    if (win->pixels != NULL)
    {
        vmm_free((void *)win->pixels);
    }

    if (win->title != NULL)
    {
        kfree((void *)win->title);
    }

    Rect *r = win->clip_list;
    while (r)
    {
        Rect *nxt = r->next;
        rect_free(r);
        r = nxt;
    }
    kfree(win);

    if (g_win_top != NULL && !(g_win_top->flags & WIN_BORDERLESS))
    {
        win_redraw_titlebar(g_win_top);
    }
    win_draw_taskbar();
    if (g_win_list != NULL)
    {
        win_stain_list(g_win_list);
    }
}

void win_draw_char_at(Window *win, char c, uint64_t x, uint64_t y, GBA_Color fg_color, GBA_Color bg_color)
{
    if (x >= win->width || y >= win->height)
    {
        return;
    }

    char *glyph = font8x8_basic[(int)c];
    for (uint8_t dy = 0; dy < CHAR_H; dy++)
    {
        for (uint8_t dx = 0; dx < CHAR_W; dx++)
        {
            if (x + dx >= win->width)
            {
                break;
            }

            uint64_t idx = (y + dy) * win->width + (x + dx);

            if (idx >= win->pixels_size / sizeof(Pixel))
            {
                continue;
            }

            win->pixels[idx].color = (glyph[dy] >> dx) & 1
                                         ? (uint32_t)fg_color
                                         : (uint32_t)bg_color;
        }
    }

    win->flags |= WIN_DIRTY;
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

        if (scrn_x >= win->x + (int64_t)win->width - WIN_BORDER_SIZE)
        {
            win->cursor_x = 0;
            win->cursor_y += CHAR_H;
        }

        win_draw_char_at(win, c, win->cursor_x + WIN_BORDER_SIZE, win->cursor_y + WIN_TITLE_BAR_HEIGHT, Black, Slate);
        win->cursor_x += CHAR_W;
    }
}

static void handle_taskbar_click(int64_t mx, int64_t my)
{
    int scrn_w = (int)video_get_width();
    int scrn_h = (int)video_get_height();
    int bar_y = scrn_h - TASKBAR_HEIGHT;
    int btn_y = bar_y + 4;
    int btn_h = TASKBAR_HEIGHT - 8;

    if (my < btn_y || my >= btn_y + btn_h)
    {
        return;
    }

    if (mx >= 4 && mx < 66)
    {
        g_pending_launch = LAUNCH_TERMINAL;
        return;
    }
    if (mx >= 70 && mx < 132)
    {
        g_pending_launch = LAUNCH_SNAKE;
        return;
    }
    if (mx >= 136 && mx < 198)
    {
        g_pending_launch = LAUNCH_CLOCK;
        return;
    }

    int curr_x = 212;
    int max_x = scrn_w - 84;
    Window *w = g_win_list;
    while (w != NULL)
    {
        if (!(w->flags & WIN_BORDERLESS))
        {
            if (curr_x + 104 <= max_x)
            {
                if (mx >= curr_x && mx < curr_x + 100)
                {
                    if (w->state == WIN_STATE_MINIMIZED)
                    {
                        win_toggle_minimize(w);
                        win_focus(w);
                        Event e;
                        e.type = EVENT_WIN_RESIZE;
                        e.modifiers = 0;
                        e.resize_event.win_owner_pid = w->owner_pid;
                        event_queue_push(&g_event_queue, e);
                    }
                    else if (w == g_win_top)
                    {
                        if (w->flags & WIN_MINIMIZABLE)
                        {
                            win_toggle_minimize(w);
                            Event e;
                            e.type = EVENT_WIN_RESIZE;
                            e.modifiers = 0;
                            e.resize_event.win_owner_pid = w->owner_pid;
                            event_queue_push(&g_event_queue, e);
                        }
                    }
                    else
                    {
                        win_focus(w);
                    }
                    win_draw_taskbar();
                    return;
                }
                curr_x += 104;
            }
        }
        w = w->next;
    }
}

static uint8_t prev_left_btn = 0;
static uint8_t prev_right_btn = 0;

void win_update(void)
{
    cli();

    int64_t mstat = mouse_get_stat();
    int64_t mx = mouse_get_x();
    int64_t my = mouse_get_y();

    CursorType nxt_cursor_typ = CURSOR_ARROW;
    uint8_t is_left_btn = (mstat & 0x01) ? 1 : 0;
    uint8_t is_right_btn = (mstat & 0x02) ? 1 : 0;
    uint8_t is_just_pressed = is_left_btn && !prev_left_btn;
    uint8_t is_right_just_pressed = is_right_btn && !prev_right_btn;

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
                    nxt_cursor_typ = CURSOR_RESIZE_H;
                }
                else if (drag_ctx.resize_dir & RES_RIGHT)
                {
                    int64_t tmp_w = mx - drag_ctx.target->x;
                    if (tmp_w >= WIN_MIN_W && tmp_w != new_w)
                    {
                        new_w = tmp_w;
                        changed = 1;
                    }
                    nxt_cursor_typ = CURSOR_RESIZE_H;
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
                    nxt_cursor_typ = CURSOR_RESIZE_V;
                }
                else if (drag_ctx.resize_dir & RES_BOTTOM)
                {
                    int64_t max_y = (int64_t)video_get_height() - TASKBAR_HEIGHT;
                    int64_t clamped_my = (my > max_y) ? max_y : my;
                    int64_t tmp_h = clamped_my - drag_ctx.target->y;
                    if (tmp_h >= WIN_MIN_H && tmp_h != new_h)
                    {
                        new_h = tmp_h;
                        changed = 1;
                    }
                    nxt_cursor_typ = CURSOR_RESIZE_V;
                }

                if (changed)
                {
                    win_resize(drag_ctx.target, new_x, new_y, new_w, new_h);
                    win_stain_list(g_win_list);
                    if (drag_ctx.target->state != WIN_STATE_MINIMIZED)
                    {
                        drag_ctx.target->state = WIN_STATE_NORMAL;
                    }
                }
            }
            else
            {
                nxt_cursor_typ = CURSOR_MOVE;
                int64_t target_y = my - drag_ctx.off_y;
                int64_t max_y = (int64_t)video_get_height() - TASKBAR_HEIGHT - WIN_TITLE_BAR_HEIGHT;
                if (target_y > max_y)
                {
                    target_y = max_y;
                }
                if (target_y < 0)
                {
                    target_y = 0;
                }
                win_move(drag_ctx.target, mx - drag_ctx.off_x, target_y);
                win_stain_list(g_win_list);
                if (drag_ctx.target->state != WIN_STATE_MINIMIZED)
                {
                    drag_ctx.target->state = WIN_STATE_NORMAL;
                }
            }
        }
        else
        {
            if (is_just_pressed && my >= (int64_t)video_get_height() - TASKBAR_HEIGHT)
            {
                handle_taskbar_click(mx, my);
            }
            else
            {
                Window *curr_win = get_win_at(mx, my);
                if (curr_win != NULL && curr_win != g_desktop_win)
                {
                    int64_t off_mx = mx - curr_win->x;
                    int64_t off_my = my - curr_win->y;

                    int64_t btn_size = WIN_TITLE_BAR_HEIGHT;
                    int64_t close_btn_x = curr_win->width - btn_size;
                    int64_t max_btn_x = curr_win->width - (2 * btn_size);
                    int64_t min_btn_x = curr_win->width - (3 * btn_size);
                    uint8_t btn_clicked = 0;

                    if (is_just_pressed)
                    {
                        if (is_point_in_rect(off_mx, off_my, close_btn_x, 0, btn_size, btn_size))
                        {
                            sched_kill((int)curr_win->owner_pid);
                            btn_clicked = 1;
                        }
                        else if ((curr_win->flags & WIN_RESIZABLE) && is_point_in_rect(off_mx, off_my, max_btn_x, 0, btn_size, btn_size))
                        {
                            win_focus(curr_win);
                            win_toggle_maximize(curr_win);
                            Event e;
                            e.type = EVENT_WIN_RESIZE;
                            e.modifiers = 0;
                            e.resize_event.win_owner_pid = curr_win->owner_pid;
                            event_queue_push(&g_event_queue, e);
                            btn_clicked = 1;
                        }
                        else if ((curr_win->flags & WIN_MINIMIZABLE) && is_point_in_rect(off_mx, off_my, min_btn_x, 0, btn_size, btn_size))
                        {
                            win_toggle_minimize(curr_win);
                            win_draw_taskbar();
                            Event e;
                            e.type = EVENT_WIN_RESIZE;
                            e.modifiers = 0;
                            e.resize_event.win_owner_pid = curr_win->owner_pid;
                            event_queue_push(&g_event_queue, e);
                            btn_clicked = 1;
                        }
                    }

                    if (!btn_clicked && drag_ctx.target == NULL)
                    {
                        if (is_just_pressed && !(curr_win->flags & WIN_BORDERLESS))
                        {
                            win_focus(curr_win);
                        }
                        uint32_t res_dir = get_resize_dir(curr_win, mx, my);

                        if (is_just_pressed && (res_dir != RES_NONE || check_win_drag(curr_win, mx, my)))
                        {
                            drag_ctx.target = curr_win;
                            drag_ctx.off_x = off_mx;
                            drag_ctx.off_y = off_my;
                            drag_ctx.resize_dir = res_dir;
                        }
                        else if (is_just_pressed && curr_win->state != WIN_STATE_MINIMIZED)
                        {
                            int64_t client_x = off_mx - WIN_BORDER_SIZE;
                            int64_t client_y = off_my - (WIN_TITLE_BAR_HEIGHT + WIN_BORDER_SIZE);
                            int cw = 0, ch = 0;
                            win_get_client_size(curr_win, &cw, &ch);
                            if (client_x >= 0 && client_x < cw && client_y >= 0 && client_y < ch)
                            {
                                Event e;
                                e.type = MOUSE_CLICK;
                                e.modifiers = 0;
                                if (keyboard_ctrl_presssed())
                                    e.modifiers |= MOD_CTRL;
                                if (keyboard_alt_presssed())
                                    e.modifiers |= MOD_ALT;
                                if (keyboard_shift_presssed())
                                    e.modifiers |= MOD_SHIFT;
                                e.mouse.x = (int)client_x;
                                e.mouse.y = (int)client_y;
                                e.mouse.buttons = (uint8_t)(mstat & 0x07);
                                win_send_event_to_owner(curr_win->owner_pid, e);
                            }
                        }

                        if (res_dir & (RES_LEFT | RES_RIGHT))
                        {
                            nxt_cursor_typ = CURSOR_RESIZE_H;
                        }
                        else if (res_dir & (RES_TOP | RES_BOTTOM))
                        {
                            nxt_cursor_typ = CURSOR_RESIZE_V;
                        }
                    }
                }
            }
        }
    }
    else
    {
        if (drag_ctx.target != NULL && drag_ctx.resize_dir != RES_NONE)
        {
            Event e;
            e.type = EVENT_WIN_RESIZE;
            e.modifiers = 0;
            e.resize_event.win_owner_pid = drag_ctx.target->owner_pid;
            event_queue_push(&g_event_queue, e);
            init_win_pixels(drag_ctx.target);
            drag_ctx.target->flags |= WIN_DIRTY;
        }
        drag_ctx.target = NULL;
        Window *hover_win = get_win_at(mx, my);
        if (hover_win != NULL && hover_win != g_desktop_win)
        {
            uint32_t res_dir = get_resize_dir(hover_win, mx, my);
            if (res_dir & (RES_LEFT | RES_RIGHT))
            {
                nxt_cursor_typ = CURSOR_RESIZE_H;
            }
            else if (res_dir & (RES_TOP | RES_BOTTOM))
            {
                nxt_cursor_typ = CURSOR_RESIZE_V;
            }

            if (is_right_just_pressed && hover_win->state != WIN_STATE_MINIMIZED)
            {
                win_focus(hover_win);
                int64_t client_x = (mx - hover_win->x) - WIN_BORDER_SIZE;
                int64_t client_y = (my - hover_win->y) - (WIN_TITLE_BAR_HEIGHT + WIN_BORDER_SIZE);
                int cw = 0, ch = 0;
                win_get_client_size(hover_win, &cw, &ch);
                if (client_x >= 0 && client_x < cw && client_y >= 0 && client_y < ch)
                {
                    Event e;
                    e.type = MOUSE_CLICK;
                    e.modifiers = 0;
                    e.mouse.x = (int)client_x;
                    e.mouse.y = (int)client_y;
                    e.mouse.buttons = (uint8_t)(mstat & 0x07);
                    win_send_event_to_owner(hover_win->owner_pid, e);
                }
            }
        }
    }

    prev_left_btn = is_left_btn;
    prev_right_btn = is_right_btn;
    cursor_set_shape(nxt_cursor_typ);

    sti();
}

Window *win_get_active(void)
{
    return g_win_top;
}

bool is_point_in_rect(int64_t px, int64_t py, int64_t rx, int64_t ry, int64_t rw, int64_t rh)
{
    return (
        px >= rx && px < rx + rw &&
        py >= ry && py < ry + rh);
}

uint8_t rect_intersect(Rect *r1, Rect *r2)
{
    return (r1->x + (int64_t)r1->w <= r2->x ||
            r1->x >= r2->x + (int64_t)r2->w ||
            r1->y + (int64_t)r1->h <= r2->y ||
            r1->y >= r2->y + (int64_t)r2->h)
               ? 0
               : 1;
}

Rect *clip_rect(Rect *r, Rect *clipper)
{
    if (!(rect_intersect(r, clipper)))
    {
        Rect *copy = rect_alloc();
        if (copy == NULL)
        {
            return NULL;
        }
        copy->x = r->x;
        copy->y = r->y;
        copy->w = r->w;
        copy->h = r->h;
        copy->next = NULL;
        return copy;
    }

    Rect *head = NULL;
    Rect *tail = NULL;

#define APPEND_RECT(_x, _y, _w, _h) \
    {                               \
        Rect *node = rect_alloc();  \
        if (node)                   \
        {                           \
            node->x = (_x);         \
            node->y = (_y);         \
            node->w = (_w);         \
            node->h = (_h);         \
            node->next = NULL;      \
            if (head == NULL)       \
            {                       \
                head = node;        \
                tail = node;        \
            }                       \
            else                    \
            {                       \
                tail->next = node;  \
                tail = node;        \
            }                       \
        }                           \
    }

    int64_t rx = r->x;
    int64_t ry = r->y;
    int64_t rw = r->w;
    int64_t rh = r->h;

    if (clipper->x > rx && clipper->x < rx + (int64_t)rw)
    {
        uint64_t new_w = clipper->x - rx;
        APPEND_RECT(rx, ry, new_w, rh);
        rx = clipper->x;
        rw -= new_w;
    }

    int64_t clipper_right = clipper->x + clipper->w;
    int64_t rect_right = rx + rw;
    if (clipper_right < rect_right && clipper_right > rx)
    {
        uint64_t new_w = rect_right - clipper_right;
        APPEND_RECT(clipper_right, ry, new_w, rh);
        rw -= new_w;
    }

    if (clipper->y > ry && clipper->y < ry + (int64_t)rh)
    {
        uint64_t new_h = clipper->y - ry;
        APPEND_RECT(rx, ry, rw, new_h);
        ry = clipper->y;
        rh -= new_h;
    }

    int64_t clipper_bot = clipper->y + clipper->h;
    int64_t rect_bot = ry + rh;
    if (clipper_bot < rect_bot && clipper_bot > ry)
    {
        uint64_t new_h = rect_bot - clipper_bot;
        APPEND_RECT(rx, clipper_bot, rw, new_h);
        rh -= new_h;
    }

    return head;
}

void recalc_clip_list(Window *win)
{
    Rect *old_rect = win->clip_list;
    while (old_rect != NULL)
    {
        Rect *next = old_rect->next;
        rect_free(old_rect);
        old_rect = next;
    }
    win->clip_list = NULL;

    Rect *new_rect = rect_alloc();
    if (new_rect == NULL)
    {
        kprint("Panic: OOM in recalc_clip_list\n");
        return;
    }
    new_rect->x = win->x;
    new_rect->y = win->y;
    new_rect->w = win->width;
    new_rect->h = win->height;
    new_rect->next = NULL;

    uint64_t max_screen_h = (win == g_desktop_win) ? video_get_height() : (video_get_height() - TASKBAR_HEIGHT);

    if (new_rect->x < 0)
    {
        new_rect->w += new_rect->x;
        new_rect->x = 0;
    }
    if (new_rect->x + new_rect->w > video_get_width())
    {
        new_rect->w = video_get_width() - new_rect->x;
    }
    if (new_rect->y < 0)
    {
        new_rect->h += new_rect->y;
        new_rect->y = 0;
    }
    if (new_rect->y + new_rect->h > max_screen_h)
    {
        if ((uint64_t)new_rect->y >= max_screen_h)
        {
            rect_free(new_rect);
            return;
        }
        new_rect->h = max_screen_h - new_rect->y;
    }

    if ((int64_t)new_rect->w <= 0 || (int64_t)new_rect->h <= 0)
    {
        rect_free(new_rect);
        return;
    }

    win->clip_list = new_rect;

    Window *blocker_win = win->next;
    Rect *new_list_head = NULL;
    Rect *new_list_tail = NULL;
    Rect *curr_list = win->clip_list;

    while (blocker_win != NULL)
    {
        Rect blocker_rect;
        blocker_rect.x = blocker_win->x;
        blocker_rect.y = blocker_win->y;
        blocker_rect.w = blocker_win->width;
        blocker_rect.h = blocker_win->height;
        blocker_rect.next = NULL;

        while (curr_list != NULL)
        {
            Rect *fragments = clip_rect(curr_list, &blocker_rect);
            if (fragments != NULL)
            {
                if (new_list_head == NULL)
                {
                    new_list_head = fragments;
                    new_list_tail = fragments;
                }
                else
                {
                    new_list_tail->next = fragments;
                }

                while (new_list_tail->next != NULL)
                {
                    new_list_tail = new_list_tail->next;
                }
            }
            Rect *old = curr_list;
            curr_list = curr_list->next;
            rect_free(old);
        }
        blocker_win = blocker_win->next;
        curr_list = new_list_head;
        new_list_head = NULL;
        new_list_tail = NULL;
    }

    win->clip_list = curr_list;
}

void win_move(Window *win, int64_t new_x, int64_t new_y)
{
    video_add_dirty_rect(win->x, win->y, win->width, win->height);
    win->x = new_x;
    win->y = new_y;
    video_add_dirty_rect(new_x, new_y, win->width, win->height);
}

void win_resize(Window *win, int64_t new_x, int64_t new_y, int64_t new_w, int64_t new_h)
{
    int64_t new_pixels_size = new_h * new_w * sizeof(Pixel);
    Pixel *new_pixels = (Pixel *)vmm_alloc_global(new_pixels_size);

    if (new_pixels != NULL)
    {
        memset_sse(new_pixels, 0, new_pixels_size);
        vmm_free(win->pixels);

        video_add_dirty_rect(win->x, win->y, win->width, win->height);
        win->pixels = new_pixels;
        win->pixels_size = new_pixels_size;
        win->x = new_x;
        win->y = new_y;
        win->width = new_w;
        win->height = new_h;
        init_win_pixels(win);
        video_add_dirty_rect(new_x, new_y, win->width, win->height);
        win->flags |= WIN_DIRTY;
    }
}

static void init_rect_pool(void)
{
    for (int i = 0; i < MAX_RECT_POOL - 1; i++)
    {
        g_rect_storage[i].next = &g_rect_storage[i + 1];
    }
    g_rect_storage[MAX_RECT_POOL - 1].next = NULL;

    g_rect_free_list = &g_rect_storage[0];
}

Rect *rect_alloc(void)
{
    if (g_rect_free_list == NULL)
    {
        kprint("ALERT: Rect Pool exhausted!\n");
        return NULL;
    }

    Rect *r = g_rect_free_list;
    g_rect_free_list = g_rect_free_list->next;
    r->next = NULL;
    return r;
}

void rect_free(Rect *r)
{
    if (r == NULL)
    {
        return;
    }

    r->next = g_rect_free_list;
    g_rect_free_list = r;
}

int win_toggle_maximize(Window *win)
{
    int64_t usable_h = (int64_t)video_get_height() - TASKBAR_HEIGHT;
    if (win->state == WIN_STATE_NORMAL)
    {
        win->pre_x = win->x;
        win->pre_y = win->y;
        win->pre_w = win->width;
        win->pre_h = win->height;
        win->state = WIN_STATE_MAXIMIZED;
        win_resize(win, 0, 0, video_get_width(), usable_h);
    }
    else if (win->state == WIN_STATE_MAXIMIZED)
    {
        win->state = WIN_STATE_NORMAL;
        win_resize(win, win->pre_x, win->pre_y, win->pre_w, win->pre_h);
    }
    else if (win->state == WIN_STATE_MINIMIZED)
    {
        win->state = WIN_STATE_MAXIMIZED;
        win_resize(win, 0, 0, video_get_width(), usable_h);
    }

    win_draw_taskbar();
    win_stain_list(g_win_list);
    return 0;
}

int win_toggle_minimize(Window *win)
{
    if (win->state == WIN_STATE_MINIMIZED)
    {
        win->state = win->pre_state;
        win_resize(win, win->x, win->y, win->width, win->pre_h);
    }
    else
    {
        win->pre_state = win->state;
        win->pre_x = win->x;
        win->pre_y = win->y;
        win->pre_w = win->width;
        win->pre_h = win->height;
        win->state = WIN_STATE_MINIMIZED;
        win_resize(win, win->x, win->y, win->width, WIN_TITLE_BAR_HEIGHT);
    }

    win_draw_taskbar();
    win_stain_list(g_win_list);
    return 0;
}

void win_draw_string(Window *win, int x, int y, char *str, uint32_t fg_color, uint32_t bg_color)
{
    int curr_x = x;
    while (*str)
    {
        win_draw_char_at(win, *str, curr_x, y, fg_color, bg_color);
        curr_x += 8;
        str++;
    }
}

void init_desktop(void)
{
    g_desktop_win = win_create(0, 0, video_get_width(), video_get_height(), NULL, WIN_BORDERLESS);

    for (uint64_t y = 0; y < g_desktop_win->height - TASKBAR_HEIGHT; y++)
    {
        for (uint64_t x = 0; x < g_desktop_win->width; x++)
        {
            bool is_dark = ((x / 4) + (y / 4)) % 2;
            g_desktop_win->pixels[y * g_desktop_win->width + x].color = is_dark ? Teal : DarkTeal;
        }
    }

    win_draw_string(g_desktop_win, 10, 10, "Welcome to NyanOS64 Desktop!", White, Teal);
    win_draw_string(g_desktop_win, 10, 22, "Hotkeys: [Ctrl+Alt+T] Terminal | [Alt+Tab] Switch Window", White, Teal);
    win_draw_taskbar();

    g_desktop_win->flags |= WIN_DIRTY;
}

void win_fill_rect(Window *win, int client_x, int client_y, int w, int h, uint32_t color)
{
    uint64_t rflags = read_rflags();
    cli();
    int x = client_x;
    int y = client_y;
    int l_lim = 0;
    int r_lim = (int)win->width;
    int t_lim = 0;
    int b_lim = (int)win->height;
    int buf_off_x = 0;
    int buf_off_y = 0;

    if (!(win->flags & WIN_BORDERLESS))
    {
        x += WIN_BORDER_SIZE;
        y += WIN_TITLE_BAR_HEIGHT + WIN_BORDER_SIZE;
        l_lim += WIN_BORDER_SIZE;
        r_lim -= WIN_BORDER_SIZE;
        t_lim += WIN_TITLE_BAR_HEIGHT + WIN_BORDER_SIZE;
        b_lim -= WIN_BORDER_SIZE;
    }

    if (x >= r_lim || y >= b_lim)
    {
        if (rflags & (1 << 9))
        {
            sti();
        }
        return;
    }

    int draw_w = w;
    int draw_h = h;

    if (x + draw_w > r_lim)
        draw_w = r_lim - x;
    if (y + draw_h > b_lim)
        draw_h = b_lim - y;

    if (x < l_lim)
    {
        buf_off_x = l_lim - x;
        draw_w -= buf_off_x;
        x = l_lim;
    }

    if (y < t_lim)
    {
        buf_off_y = t_lim - y;
        draw_h -= buf_off_y;
        y = t_lim;
    }

    if (draw_w <= 0 || draw_h <= 0)
    {
        if (rflags & (1 << 9))
        {
            sti();
        }
        return;
    }

    for (int r = 0; r < draw_h; r++)
    {
        uint64_t win_idx = (y + r) * win->width + x;
        memset32_sse(&win->pixels[win_idx], color, draw_w);
    }

    win->flags |= WIN_DIRTY;
    if (rflags & (1 << 9))
    {
        sti();
    }
}

void win_draw_bitmap(Window *win, int client_x, int client_y, int w, int h, uint32_t *buf)
{
    uint64_t rflags = read_rflags();
    cli();

    int x = client_x;
    int y = client_y;
    int l_lim = 0;
    int r_lim = (int)win->width;
    int t_lim = 0;
    int b_lim = (int)win->height;
    int buf_off_x = 0;
    int buf_off_y = 0;

    if (!(win->flags & WIN_BORDERLESS))
    {
        x += WIN_BORDER_SIZE;
        y += WIN_TITLE_BAR_HEIGHT + WIN_BORDER_SIZE;
        l_lim += WIN_BORDER_SIZE;
        r_lim -= WIN_BORDER_SIZE;
        t_lim += WIN_TITLE_BAR_HEIGHT + WIN_BORDER_SIZE;
        b_lim -= WIN_BORDER_SIZE;
    }

    if (x >= r_lim || y >= b_lim)
    {
        if (rflags & (1 << 9))
        {
            sti();
        }
        return;
    }

    int draw_w = w;
    int draw_h = h;

    if (x + draw_w > r_lim)
        draw_w = r_lim - x;
    if (y + draw_h > b_lim)
        draw_h = b_lim - y;

    if (x < l_lim)
    {
        buf_off_x = l_lim - x;
        draw_w -= buf_off_x;
        x = l_lim;
    }

    if (y < t_lim)
    {
        buf_off_y = t_lim - y;
        draw_h -= buf_off_y;
        y = t_lim;
    }

    if (draw_w <= 0 || draw_h <= 0)
    {
        if (rflags & (1 << 9))
        {
            sti();
        }
        return;
    }

    for (int r = 0; r < draw_h; r++)
    {
        uint64_t win_idx = (y + r) * win->width + x;
        uint64_t buf_idx = (r + buf_off_y) * w + buf_off_x;
        memcpy_sse(&win->pixels[win_idx], &buf[buf_idx], draw_w * sizeof(uint32_t));
    }

    win->flags |= WIN_DIRTY;
    if (rflags & (1 << 9))
    {
        sti();
    }
}

void win_get_client_size(Window *win, int *out_w, int *out_h)
{
    if (win == NULL)
    {
        return;
    }

    if (win->flags & WIN_BORDERLESS)
    {
        if (out_w)
        {
            *out_w = win->width;
        }
        if (out_h)
        {
            *out_h = win->height;
        }
    }
    else
    {
        if (out_w)
        {
            *out_w = win->width - (WIN_BORDER_SIZE * 2);
        }
        if (out_h)
        {
            *out_h = win->height - (WIN_TITLE_BAR_HEIGHT + WIN_BORDER_SIZE * 2);
        }
    }
}