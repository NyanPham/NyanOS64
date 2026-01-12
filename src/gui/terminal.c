#include "terminal.h"
#include "mem/kmalloc.h"
#include "mem/vmm.h"
#include "sched/sched.h"
#include "drivers/keyboard.h"
#include "../string.h"
#include "kern_defs.h"
#include "utils/math.h"

#define SCROLL_INTERVAL 3

/**
 * @brief Computes the index
 * Normally, we just do
 * idx = row * cells_per_row + col.
 * But with circular buffer, we do
 * idx = ((start_line + row) % n_rows) + col.
 */
static inline uint64_t term_get_idx(Terminal *term, uint64_t row, uint64_t col)
{
    return ((term->start_line_idx + row) % term->n_rows) * term->n_cols + col;
}

static inline void term_scroll_data(Terminal *term)
{
    memset(&term->text_buf[term->start_line_idx * term->n_cols], 0, term->text_buf_siz / term->n_rows);
    term->start_line_idx = (term->start_line_idx + 1) % term->n_rows;
    term->cur_row = term->n_rows - 1;
}

static inline void term_stain_row(Terminal *term, uint64_t row_idx)
{
    if (term->dirty_start_row_idx == -1)
    {
        term->dirty_start_row_idx = row_idx;
        term->dirty_end_row_idx = row_idx;
        return;
    }

    if ((int64_t)row_idx < term->dirty_start_row_idx)
    {
        term->dirty_start_row_idx = row_idx;
    }
    if ((int64_t)row_idx > term->dirty_end_row_idx)
    {
        term->dirty_end_row_idx = row_idx;
    }
}

/* START: ANSI DRIVER IMPLEMENTATION FOR TERMINAL */
static void term_driver_put_char(void *ctx, char c)
{
    Terminal *term = (Terminal *)ctx;
    term_stain_row(term, term->cur_row);

    if (c == '\n')
    {
        term->cur_col = 0;
        term->cur_row++;
    }
    else if (c == '\b')
    {
        if (term->cur_col > 0)
        {
            term->cur_col--;
            uint64_t idx = term_get_idx(term, term->cur_row, term->cur_col);
            term->text_buf[idx].glyph = ' ';
            term->text_buf[idx].color = Slate;
        }
    }
    else
    {
        // record into the buffer
        uint64_t idx = term_get_idx(term, term->cur_row, term->cur_col);
        if (idx < term->n_rows * term->n_cols)
        {
            term->text_buf[idx].glyph = c;
            term->text_buf[idx].color = term->ansi_ctx.color;
        }
        term->cur_col++;

        if (term->cur_col >= term->n_cols)
        {
            term->cur_col = 0;
            term->cur_row++;
        }
    }

    // Handle the scroll the data
    if (term->cur_row >= term->n_rows)
    {
        term_scroll_data(term);
    }

    // handle the scroll in the view
    uint64_t win_rows = (term->win->height - WIN_TITLE_BAR_H) / CHAR_H;
    uint8_t view_changed = 0;
    if (term->cur_row >= term->scroll_idx + win_rows)
    {
        term->scroll_idx = term->cur_row - win_rows + 1;
        view_changed = 1;
    }
    else if (term->cur_row < term->scroll_idx)
    {
        term->scroll_idx = term->cur_row;
        view_changed = 1;
    }

    if (view_changed)
    {
        term_stain_row(term, term->scroll_idx);
        term_stain_row(term, term->scroll_idx + win_rows);
    }
    term_stain_row(term, term->cur_row);
    term->flags |= TERM_DIRTY;
}

static void term_driver_set_color(void *ctx, uint32_t color)
{
    (void)ctx;
    (void)color;
}

static void term_driver_set_cursor(void *ctx, int x, int y)
{
    Terminal *term = (Terminal *)ctx;
    term->cur_col = x;
    term->cur_row = y;

    // check boundary
    if (term->cur_col >= term->n_cols)
    {
        term->cur_col = term->n_cols - 1;
    }
    if (term->cur_row >= term->n_rows)
    {
        term->cur_row = term->n_rows - 1;
    }
}

static void term_driver_clear(void *ctx, int mode)
{
    if (mode == 2)
    {
        Terminal *term = (Terminal *)ctx;
        term->cur_row = 0;
        term->cur_col = 0;
        term->start_line_idx = 0;
        term->scroll_idx = 0;
        term->ansi_ctx.color = White;
        uint64_t n_cells = term->n_rows * term->n_cols;
        for (uint64_t i = 0; i < n_cells; i++)
        {
            term->text_buf[i].glyph = 0;
            term->text_buf[i].color = Slate;
        }

        term_stain_row(term, term->scroll_idx);
        term_stain_row(term, uint64_min(
                                 term->scroll_idx + (term->win->height - WIN_TITLE_BAR_H) / CHAR_H,
                                 term->n_rows - 1));
        term->flags |= TERM_DIRTY;
    }
}

static void term_driver_set_mode(void *ctx, int mode, uint8_t enabled)
{
    if (mode == 25)
    {
        Terminal *term = (Terminal *)ctx;
        if (enabled)
        {
            term->flags |= CURSOR_ENABLED;
        }
        else
        {
            term->flags &= ~CURSOR_ENABLED;
        }
        term->flags |= TERM_DIRTY;
    }
}

static const AnsiDriver g_term_driver = {
    .put_char = term_driver_put_char,
    .set_color = term_driver_set_color,
    .set_cursor = term_driver_set_cursor,
    .clear_screen = term_driver_clear,
    .scroll = NULL,
    .set_mode = term_driver_set_mode,
};
/* END: ANSI DRIVER IMPLEMENTATION FOR TERMINAL */

/**
 * @brief
 * x, y, w, and h are pixel values for the window (view)
 *
 * First, crates the View (window)
 * Then initializes the Data Model like n_rows, n_cols
 */
Terminal *term_create(int64_t x, int64_t y, uint64_t w, uint64_t h, uint64_t max_rows, const char *title, uint32_t win_flags)
{
    // View
    Terminal *term = (Terminal *)kmalloc(sizeof(Terminal));
    term->win = win_create(x, y, w, h, title, win_flags);

    // Data Model
    term->n_rows = max_rows;
    if (w > 2 * WIN_BORDER_SIZE)
    {
        term->n_cols = (w - (2 * WIN_BORDER_SIZE)) / CHAR_W;
    }
    else
    {
        term->n_cols = 1;
    }

    uint64_t buf_size = term->n_rows * term->n_cols * sizeof(TermCell);
    term->text_buf = (TermCell *)vmm_alloc(buf_size);
    if (term->text_buf == NULL)
    {
        // PANIC
        kprint("Panic: term_create cannot vmm_alloc for text_buf\n");
        return NULL;
    }
    term->text_buf_siz = buf_size;
    memset(term->text_buf, 0, buf_size);

    // State
    term->start_line_idx = 0;
    term->scroll_idx = 0;
    term->cur_row = 0;
    term->cur_col = 0;
    term->dirty_start_row_idx = -1;
    term->dirty_end_row_idx = -1;
    term->child_pid = -1;

    rb_init(&term->input_buf);

    term->ansi_ctx.color = Black;
    term->ansi_ctx.state = ANSI_NORMAL;
    term->ansi_ctx.idx = 0;
    memset(term->ansi_ctx.buf, 0, ANSI_BUF_SIZE);
    term->flags = CURSOR_ENABLED | TERM_DIRTY;

    return term;
}

/**
 * @brief
 */
void term_destroy(Terminal *term)
{
    if (term == NULL)
    {
        return;
    }

    if (term->text_buf != NULL)
    {
        vmm_free(term->text_buf);
        term->text_buf = NULL;
        term->text_buf_siz = 0;
    }

    if (term->win != NULL)
    {
        win_close(term->win);
        term->win = NULL;
    }

    kfree(term);
}

static void draw_text_cursor(Terminal *term, uint64_t win_rows)
{
    if ((term->flags & (CURSOR_ENABLED | CURSOR_STATE)) != (CURSOR_ENABLED | CURSOR_STATE))
    {
        return;
    }

    int64_t visual_row = term->cur_row - term->scroll_idx;
    if (visual_row >= 0 && visual_row < win_rows)
    {
        int64_t px = term->cur_col * CHAR_W + WIN_BORDER_SIZE;
        int64_t py = visual_row * CHAR_H + WIN_TITLE_BAR_H;
        for (int y = 0; y < CHAR_H; y++)
        {
            for (int x = 0; x < CHAR_W; x++)
            {
                int64_t pix_idx = (py + y) * term->win->width + (px + x);
                if (pix_idx < term->win->pixels_size / sizeof(Pixel))
                {
                    uint32_t curr_color = term->win->pixels[pix_idx].color;
                    term->win->pixels[pix_idx].color = curr_color == Slate ? White : Black;
                }
            }
        }
    }
}

static void term_redraw_cursor_cell(Terminal *term)
{
    // is the cursor within the view?
    uint64_t win_rows = (term->win->height - WIN_TITLE_BAR_H) / CHAR_H;
    int64_t visual_row = term->cur_row - term->scroll_idx;

    if (visual_row < 0 || visual_row >= win_rows)
    {
        return;
    }

    // get the content at the cursor
    uint64_t idx = term_get_idx(term, term->cur_row, term->cur_col);
    TermCell cell = term->text_buf[idx];

    // compute the cell coords in pixels
    int64_t px = term->cur_col * CHAR_W + WIN_BORDER_SIZE;
    int64_t py = visual_row * CHAR_H + WIN_TITLE_BAR_H;

    // which color
    char ch = (cell.glyph == 0) ? ' ' : cell.glyph;
    uint32_t fg_color = (cell.color == 0) ? Black : cell.color;
    uint32_t bg_color = Slate;

    // paint the background at the cursor first
    win_draw_char_at(term->win, ch, px, py, fg_color, bg_color);

    // cursor is enabled and on, then paint the foreground
    if ((term->flags & (CURSOR_ENABLED | CURSOR_STATE)) == (CURSOR_ENABLED | CURSOR_STATE))
    {
        for (int y = 0; y < CHAR_H; y++)
        {
            for (int x = 0; x < CHAR_W; x++)
            {
                int64_t pix_idx = (py + y) * term->win->width + (px + x);
                if (pix_idx < term->win->pixels_size / sizeof(Pixel))
                {
                    term->win->pixels[pix_idx].color = White;
                }
            }
        }
    }
}

/**
 * @brief Renders by scanning the buffer and paint on window
 */
void term_refresh(Terminal *term)
{
    if (term->dirty_start_row_idx < 0 || term->dirty_end_row_idx < 0)
    {
        return;
    }

    // viewport is identified with scroll_idx
    uint64_t content_h = 0;
    if (term->win->height > (WIN_TITLE_BAR_H + WIN_BORDER_SIZE))
    {
        content_h = term->win->height - WIN_TITLE_BAR_H - WIN_BORDER_SIZE;
    }

    uint64_t win_rows = content_h / CHAR_H;

    for (uint64_t r = term->dirty_start_row_idx; r <= term->dirty_end_row_idx; r++)
    {
        if (r < term->scroll_idx || r >= term->scroll_idx + win_rows)
        {
            continue;
        }

        uint64_t buf_row = (term->start_line_idx + r) % term->n_rows;

        for (uint64_t c = 0; c < term->n_cols; c++)
        {
            TermCell cell = term->text_buf[term_get_idx(term, buf_row, c)];

            uint64_t px = c * CHAR_W;                      // convert to pixel x
            uint64_t py = (r - term->scroll_idx) * CHAR_H; // convert to pixel y

            char ch = (cell.glyph == 0) ? ' ' : cell.glyph;
            uint32_t color = (cell.color == 0) ? Black : cell.color;

            win_draw_char_at(term->win, ch, px + WIN_BORDER_SIZE, py + WIN_TITLE_BAR_H, color, Slate);
        }
    }

    term->dirty_start_row_idx = -1;
    term->dirty_end_row_idx = -1;
    draw_text_cursor(term, win_rows);
}

void term_put_char(Terminal *term, char c)
{
    ansi_write_char(&term->ansi_ctx, c, &g_term_driver, term);
}

static void flush_input_queue(Terminal *term)
{
    for (int8_t i = 0; i < term->input_queue_idx; i++)
    {
        rb_push(&term->input_buf, term->input_queue[i]);
    }
    term->input_queue_idx = 0;
    term->input_state = TERM_INPUT_NORMAL;
}

void term_process_input(Terminal *term, char c)
{
    switch (term->input_state)
    {
    case TERM_INPUT_NORMAL:
    {
        if (c == '\033')
        {
            term->input_state = TERM_INPUT_ESC;
            term->input_queue[term->input_queue_idx++] = c;
        }
        else
        {
            rb_push(&term->input_buf, c);
        }
        break;
    }
    case TERM_INPUT_ESC:
    {
        if (c == '[')
        {
            term->input_state = TERM_INPUT_CSI;
            term->input_queue[term->input_queue_idx++] = c;
        }
        else
        {
            term->input_queue[term->input_queue_idx++] = c;
            flush_input_queue(term);
        }
        break;
    }
    case TERM_INPUT_CSI:
    {
        term->input_queue[term->input_queue_idx++] = c;

        if (c >= '0' && c <= '9')
        {
            term->input_state = TERM_INPUT_PARAM;
        }
        else if (c >= '@' && c <= '~')
        {
            flush_input_queue(term);
        }
        else
        {
            flush_input_queue(term);
        }
        break;
    }
    case TERM_INPUT_PARAM:
    {
        term->input_queue[term->input_queue_idx++] = c;

        if (c == '~')
        {
            if (term->input_queue_idx >= 4)
            {
                char param = term->input_queue[term->input_queue_idx - 2];
                if (param == '5')
                {
                    term_scroll(term, -SCROLL_INTERVAL);
                    term->input_queue_idx = 0;
                    term->input_state = TERM_INPUT_NORMAL;
                    return;
                }
                else if (param == '6')
                {
                    term_scroll(term, SCROLL_INTERVAL);
                    term->input_queue_idx = 0;
                    term->input_state = TERM_INPUT_NORMAL;
                    return;
                }
            }
            flush_input_queue(term);
        }
        else if (c >= '0' && c <= '9')
        {
            if (term->input_queue_idx >= 8)
            {
                flush_input_queue(term);
            }
        }
        else
        {
            flush_input_queue(term);
        }
        break;
    }
    }
}

size_t term_read(Terminal *term, char *buf, size_t count)
{
    size_t n = 0;
    while (n < count)
    {
        char c;

        if (rb_pop(&term->input_buf, &c))
        {
            buf[n++] = c;
            if (c == '\n')
            {
                break;
            }
        }
        else
        {
            if (n > 0)
            {
                break;
            }
            sched_block();
        }
    }
    return n;
}

void term_scroll(Terminal *term, int32_t delta)
{
    int64_t new_idx = (int64_t)term->scroll_idx + delta;
    uint64_t win_rows = (term->win->height - WIN_TITLE_BAR_H) / CHAR_H;

    // Clamping
    if (new_idx < 0)
    {
        new_idx = 0;
    }
    uint64_t max_idx = 0;
    if (term->n_rows > win_rows)
    {
        max_idx = term->n_rows - win_rows;
    }
    if (new_idx > max_idx)
    {
        new_idx = max_idx;
    }

    if (new_idx != term->scroll_idx)
    {
        term->scroll_idx = new_idx;
        term_stain_row(term, term->scroll_idx);
        term_stain_row(term, uint64_min(
                                 term->scroll_idx + (term->win->height - WIN_TITLE_BAR_H) / CHAR_H,
                                 term->n_rows - 1));
        term->flags |= TERM_DIRTY;
    }
}

Terminal *term_resize(Terminal *term, uint64_t w, uint64_t h)
{
    // View (window) has been updated, we can either based
    // on the term->win fields, or the passed arguments.

    // Safety check
    if (w < WIN_MIN_W)
    {
        w = WIN_MIN_W;
    }

    // Update Data Model
    // 1. Test if we create the new buf successfully
    uint64_t new_n_rows = term->n_rows;
    uint64_t content_w = (w > 2 * WIN_BORDER_SIZE) ? (w - 2 * WIN_BORDER_SIZE) : 0;
    uint64_t new_n_cols = content_w / CHAR_W;

    if (new_n_cols == 0)
    {
        new_n_cols = 1;
    }

    if (new_n_rows <= (2 * h / CHAR_H))
    {
        new_n_rows = 3 * h / CHAR_H;
    }

    uint64_t new_buf_size = new_n_rows * new_n_cols * sizeof(TermCell);
    TermCell *new_text_buf = (TermCell *)vmm_alloc(new_buf_size);
    if (new_text_buf == NULL)
    {
        // PANIC
        kprint("Panic: term_resize cannot vmm_alloc for new_text_buf\n");
        return term;
    }
    memset(new_text_buf, 0, new_buf_size);

    uint64_t last_valid_row = 0;
    bool found_cntnt = false;
    for (int64_t r = term->n_rows - 1; r >= 0; r--)
    {
        for (uint64_t c = 0; c < term->n_cols; c++)
        {
            TermCell cell = term->text_buf[term_get_idx(term, r, c)];
            if (cell.glyph != 0 && cell.glyph != ' ')
            {
                last_valid_row = r;
                found_cntnt = true;
                break;
            }
        }
        if (found_cntnt)
        {
            break;
        }
    }

    if (term->cur_row > last_valid_row)
    {
        last_valid_row = term->cur_row;
    }

    if (last_valid_row >= term->n_rows)
    {
        last_valid_row = term->n_rows - 1;
    }

    // 2. Copy from the old buf to the new one with wrap logic
    uint64_t new_r = 0;
    uint64_t new_c = 0;

    for (uint64_t r = 0; r <= last_valid_row; r++)
    {
        uint64_t last_c = 0;
        for (uint64_t c = 0; c < term->n_cols; c++)
        {
            if (term->text_buf[term_get_idx(term, r, c)].glyph != ' ')
            {
                last_c = c + 1;
            }
        }

        for (uint64_t c = 0; c < last_c; c++)
        {
            if (new_r >= new_n_rows)
            {
                break;
            }

            new_text_buf[new_r * new_n_cols + new_c] = term->text_buf[term_get_idx(term, r, c)];
            new_c++;

            if (new_c >= new_n_cols)
            {
                new_c = 0;
                new_r++;
            }
        }
        if (last_c == 0 || (new_c != 0 && r < last_valid_row))
        {
            new_c = 0;
            new_r++;
        }

        if (new_r >= new_n_rows)
        {
            break;
        }
    }

    // 3. swap the text bufs for the terminal
    vmm_free(term->text_buf);

    term->n_cols = new_n_cols;
    term->n_rows = new_n_rows;
    term->text_buf = new_text_buf;
    term->text_buf_siz = new_buf_size;
    term->start_line_idx = 0;

    term->cur_col = new_c;
    term->cur_row = new_r;

    if (term->cur_col >= term->n_cols)
    {
        term->cur_row++;
        term->cur_col = 0;
    }

    if (term->cur_row >= term->n_rows)
    {
        term_scroll_data(term);
    }

    term_stain_row(term, term->scroll_idx);
    term_stain_row(term, uint64_min(
                             term->scroll_idx + (term->win->height - WIN_TITLE_BAR_H) / CHAR_H,
                             term->n_rows - 1));
    term->flags |= TERM_DIRTY;

    return term;
}

static Terminal *get_curr_term()
{
    Window *curr_win = win_get_active();
    if (curr_win == NULL || curr_win->owner_pid <= 0)
    {
        return NULL;
    }

    Task *curr_tsk = sched_find_task(curr_win->owner_pid);
    if (curr_tsk == NULL || curr_tsk->term == NULL)
    {
        return NULL;
    }

    return curr_tsk->term;
}
/**
 * @brief Blinks the text cursor
 * Finds the active task (current running task)
 * and check if terminal attached exists.
 * If yes, check if it's time to turn on/off the
 * display of text cursor on the terminal.
 */
void term_blink_active()
{
    Terminal *curr_term = get_curr_term();

    if (curr_term == NULL)
    {
        return;
    }

    uint8_t text_cursor_state = ((timer_get_ticks() / 30) & 1);
    if ((curr_term->flags & CURSOR_STATE) == (text_cursor_state << 1))
    {
        return;
    }

    if (text_cursor_state)
    {
        curr_term->flags |= CURSOR_STATE;
    }
    else
    {
        curr_term->flags &= ~CURSOR_STATE;
    }

    term_redraw_cursor_cell(curr_term);
}

void term_paint()
{
    Terminal *curr_term = get_curr_term();
    if (curr_term == NULL || !(curr_term->flags & TERM_DIRTY))
    {
        return;
    }

    term_refresh(curr_term);
    curr_term->flags &= ~TERM_DIRTY;
}