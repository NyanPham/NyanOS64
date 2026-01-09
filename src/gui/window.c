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
#include "cursor.h"

#include <stddef.h>

// From font.h
extern char font8x8_basic[128][8];
extern EventBuf g_event_queue;

static Window *g_win_list = NULL; // Bottom / Head of lis
static Window *g_win_top = NULL;  // Top / Tail of List to focus

/*
Window uses Rects a lot, especially when win_stain_list runs.
Kmalloc them is a waste of time, we can use the Rect Pool.
Here I use Static Array as the memory storage of Rects, no need to alloc;
and I keep track of free rect list wih Linked List.
*/

#define MAX_RECT_POOL 2000

static Rect g_rect_storage[MAX_RECT_POOL];
static Rect *g_rect_free_list = NULL;

static void init_rect_pool(void);
Rect *rect_alloc(void);

static WinDragCtx drag_ctx =
    {
        .target = NULL,
        .off_x = 0,
        .off_y = 0,
};

static void win_stain_list(Window *win)
{
    while (win != NULL)
    {
        win->flags |= WIN_DIRTY;
        win = win->next;
    }
}

static void win_draw(Window *win)
{
    // Blitting/photocopy from win->pixels to screen
    // draw the body of window with slate grey

    Rect *rect = win->clip_list;
    if (rect == NULL)
    {
        return;
    }

    while (rect != NULL)
    {
        int64_t start_off_x = rect->x - win->x;
        int64_t start_off_y = rect->y - win->y;
        for (int r = 0; r < rect->h; r++)
        {
            uint32_t *row = &win->pixels[(r + start_off_y) * win->width + (start_off_x)];
            int64_t scrn_x = rect->x;
            int64_t scrn_y = r + rect->y;
            video_draw_pixel_line(scrn_x, scrn_y, row, rect->w * sizeof(Pixel));
        }
        video_add_dirty_rect(rect->x, rect->y, rect->w, rect->h);
        rect = rect->next;
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

Window *win_create(int64_t x, int64_t y, uint64_t width, uint64_t height, const char *title, uint32_t flags)
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

    return win;
}

void init_win_manager(void)
{
    init_rect_pool();
    kprint("Window manager inited!\n");
}

bool check_win_drag(Window *win, int64_t mouse_x, int64_t mouse_y)
{
    return (win->flags & WIN_MOVABLE) && is_point_in_rect(mouse_x, mouse_y, win->x, win->y, win->width, WIN_TITLE_BAR_H);
}

void win_paint()
{
    asm volatile("cli");
    Window *curr = g_win_list;
    while (curr != NULL)
    {
        win_draw(curr);
        curr = curr->next;
    }
    asm volatile("sti");
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
void win_focus(Window *win)
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

void win_close(Window *win)
{
    video_add_dirty_rect(win->x, win->y, win->width, win->height);

    if (win == drag_ctx.target)
    {
        drag_ctx.target = NULL;
    }

    if (win->prev == NULL && win->next == NULL) // the only one left
    {
        g_win_list = NULL;
        g_win_top = NULL;
    }
    else if (win == g_win_list) // is bottom :)
    {
        g_win_list = g_win_list->next;
        if (g_win_list)
        {
            g_win_list->prev = NULL;
        }
    }
    else if (win == g_win_top) // is top :/
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

    if (win->pixels != NULL)
    {
        vmm_free(win->pixels);
    }

    Rect *r = win->clip_list;
    while (r)
    {
        Rect *nxt = r->next;
        rect_free(r);
        r = nxt;
    }
    kfree(win);

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

void win_update(void)
{
    int64_t mstat = mouse_get_stat();
    int64_t mx = mouse_get_x();
    int64_t my = mouse_get_y();

    CursorType nxt_cursor_typ = CURSOR_ARROW;

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
                    int64_t tmp_h = my - drag_ctx.target->y;
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
                }
            }
            else
            {
                nxt_cursor_typ = CURSOR_MOVE;
                win_move(drag_ctx.target, mx - drag_ctx.off_x, my - drag_ctx.off_y);
                win_stain_list(g_win_list);
            }
        }
        else
        {
            Window *curr_win = get_win_at(mx, my);
            if (curr_win != NULL)
            {
                win_focus(curr_win);
                int64_t off_mx = mx - curr_win->x;
                int64_t off_my = my - curr_win->y;
                uint32_t res_dir = get_resize_dir(curr_win, mx, my);

                if (res_dir != RES_NONE || check_win_drag(curr_win, mx, my))
                {
                    drag_ctx.target = curr_win;
                    drag_ctx.off_x = off_mx;
                    drag_ctx.off_y = off_my;
                    drag_ctx.resize_dir = res_dir;
                }

                if (res_dir & (RES_LEFT | RES_RIGHT))
                {
                    nxt_cursor_typ = CURSOR_RESIZE_H;
                }
                else if (res_dir & (RES_TOP | RES_BOTTOM))
                {
                    nxt_cursor_typ = CURSOR_RESIZE_V;
                }

                int64_t btn_size = WIN_TITLE_BAR_H;
                int64_t btn_x = curr_win->width - btn_size;
                int64_t btn_y = 0;

                if (is_point_in_rect(off_mx, off_my, btn_x, btn_y, btn_size, btn_size))
                {
                    sched_kill(curr_win->owner_pid);
                }

                win_stain_list(g_win_list);
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
            drag_ctx.target->flags |= WIN_DIRTY;
        }
        drag_ctx.target = NULL;
        Window *hover_win = get_win_at(mx, my);
        if (hover_win != NULL)
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
        }
    }

    cursor_set_shape(nxt_cursor_typ);

    Window *curr_win = g_win_list;
    while (curr_win != NULL)
    {
        if (curr_win->flags & WIN_DIRTY)
        {
            recalc_clip_list(curr_win);
            curr_win->flags &= ~WIN_DIRTY;
        }
        curr_win = curr_win->next;
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
        *copy = *r;
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

    // left
    if (clipper->x > rx && clipper->x < rx + (int64_t)rw)
    {
        uint64_t new_w = clipper->x - rx;
        APPEND_RECT(rx, ry, new_w, rh);
        rx = clipper->x;
        rw -= new_w;
    }

    // right
    int64_t clipper_right = clipper->x + clipper->w;
    int64_t rect_right = rx + rw;
    if (clipper_right < rect_right && clipper_right > rx)
    {
        uint64_t new_w = rect_right - clipper_right;
        APPEND_RECT(clipper_right, ry, new_w, rh);
        rw -= new_w;
    }

    // top
    if (clipper->y > ry && clipper->y < ry + (int64_t)rh)
    {
        uint64_t new_h = clipper->y - ry;
        APPEND_RECT(rx, ry, rw, new_h);
        ry = clipper->y;
        rh -= new_h;
    }

    // bottom
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
    // remove the obsolete clip list
    Rect *old_rect = win->clip_list;
    while (old_rect != NULL)
    {
        Rect *next = old_rect->next;
        rect_free(old_rect);
        old_rect = next;
    }
    win->clip_list = NULL;

    // create a new rect list
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
    // make sure it doens't move out of screen bound
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
    if (new_rect->y + new_rect->h > video_get_height())
    {
        new_rect->h = video_get_height() - new_rect->y;
    }

    if (new_rect->w <= 0 || new_rect->h <= 0)
    {
        rect_free(new_rect);
        return;
    }

    win->clip_list = new_rect;

    // clip the list
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
    Pixel *new_pixels = (Pixel *)vmm_realloc(win->pixels, new_pixels_size);

    if (new_pixels != NULL)
    {
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
    // win_stain_list(g_win_list);
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