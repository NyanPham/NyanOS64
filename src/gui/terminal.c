#include "terminal.h"
#include "mem/kmalloc.h"
#include "mem/vmm.h"
#include "sched/sched.h"
#include "drivers/keyboard.h"
#include "../string.h"
#include "kern_defs.h"

#define SCROLL_INTERVAL 3

static inline void term_scroll_data(Terminal *term)
{
    TermCell *dst = term->text_buf;
    TermCell *src = term->text_buf + term->n_cols;
    uint64_t count = (term->n_rows - 1) * term->n_cols * sizeof(TermCell);

    memcpy(dst, src, count);

    TermCell *last_row = term->text_buf + (term->n_rows - 1) * term->n_cols;
    memset(last_row, 0, term->n_cols * sizeof(TermCell));

    term->cur_row = term->n_rows - 1;
}

/* START: ANSI DRIVER IMPLEMENTATION FOR TERMINAL */
static void term_driver_put_char(void *ctx, char c)
{
    Terminal *term = (Terminal *)ctx;
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
            uint64_t idx = term->cur_row * term->n_cols + term->cur_col;
            term->text_buf[idx].glyph = ' ';
            term->text_buf[idx].color = Slate;
        }
    }
    else
    {
        // record into the buffer
        uint64_t idx = term->cur_row * term->n_cols + term->cur_col;
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
    if (term->cur_row >= term->scroll_idx + win_rows)
    {
        term->scroll_idx = term->cur_row - win_rows + 1;
    }

    term_refresh(term);
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
        term->ansi_ctx.color = White;
        uint64_t n_cells = term->n_rows * term->n_cols;
        for (uint64_t i = 0; i < n_cells; i++)
        {
            term->text_buf[i].glyph = 0;
            term->text_buf[i].color = Slate;
        }

        term_refresh(term);
    }
}

static const AnsiDriver g_term_driver = {
    .put_char = term_driver_put_char,
    .set_color = term_driver_set_color,
    .set_cursor = term_driver_set_cursor,
    .clear_screen = term_driver_clear,
    .scroll = NULL,
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
    term->win = create_win(x, y, w, h, title, win_flags);

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
    term->scroll_idx = 0;
    term->cur_row = 0;
    term->cur_col = 0;
    term->child_pid = -1;

    rb_init(&term->input_buf);

    term->ansi_ctx.color = Black;
    term->ansi_ctx.state = ANSI_NORMAL;
    term->ansi_ctx.idx = 0;
    memset(term->ansi_ctx.buf, 0, ANSI_BUF_SIZE);

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
        close_win(term->win);
        term->win = NULL;
    }

    kfree(term);
}

/**
 * @brief Renders by scanning the buffer and paint on window
 */
void term_refresh(Terminal *term)
{
    // viewport is identified with scroll_idx
    uint64_t content_h = 0;
    if (term->win->height > (WIN_TITLE_BAR_H + WIN_BORDER_SIZE))
    {
        content_h = term->win->height - WIN_TITLE_BAR_H - WIN_BORDER_SIZE;
    }

    uint64_t win_rows = content_h / CHAR_H;

    for (uint64_t r = 0; r < win_rows; r++)
    {
        uint64_t buf_row = term->scroll_idx + r;

        if (buf_row >= term->n_rows) // end of buffer
        {
            break;
        }

        for (uint64_t c = 0; c < term->n_cols; c++)
        {
            TermCell cell = term->text_buf[buf_row * term->n_cols + c];

            uint64_t px = c * CHAR_W; // convert to pixel x
            uint64_t py = r * CHAR_H; // convert to pixel y

            char ch = (cell.glyph == 0) ? ' ' : cell.glyph;
            uint32_t color = (cell.color == 0) ? Black : cell.color;

            win_draw_char_at(term->win, ch, px + WIN_BORDER_SIZE, py + WIN_TITLE_BAR_H, color, Slate);
        }
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

void term_put_char(Terminal *term, char c)
{
    ansi_write_char(&term->ansi_ctx, c, &g_term_driver, term);
}

size_t term_read(Terminal *term, char *buf, size_t count)
{
    size_t n = 0;
    while (n < count)
    {
        char c = keyboard_get_char();

        // upon receiving the event of the firest ansi key within a sequence, starting with '\033' for example
        // the keyboard has already pushed the whole sequence.
        if (c == '\033') // ESC?
        {
            char seq[3] = {0};
            seq[0] = keyboard_get_char();
            seq[1] = keyboard_get_char();
            seq[2] = keyboard_get_char();
            // if it's either [5~ or [6~?
            if (
                seq[0] == '[' &&
                (seq[1] == '5' || seq[1] == '6') &&
                seq[2] == '~')
            {
                int delta = (seq[1] == '5') ? -SCROLL_INTERVAL : SCROLL_INTERVAL;
                term_scroll(term, delta);
                continue;
            }
            else
            {
                buf[n++] = c;

                for (uint8_t i = 0; i < 3; i++)
                {
                    if (n < count && seq[i] != 0)
                    {
                        buf[n++] = seq[i];
                    }
                }
                if (n > 0 && buf[n - 1] == '\n')
                {
                    break;
                }
            }
        }
        else if (c != 0)
        {
            // store to buf
            buf[n++] = c;

            // could be echoed to the screen
            // BUT, we'll let the task that owns
            // the terminal to decide what to print
            // instead :))
            // term_put_char(term, c);

            if (c == '\n')
            {
                break;
            }
        }
        else
        {
            // the input_buf now is empty. there are 2 cases

            // case 1: we've read something already
            if (n > 0)
            {
                break;
            }

            // case 2: nothing was read -> go to sleep!
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
        term_refresh(term);
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
            TermCell cell = term->text_buf[r * term->n_cols + c];
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
            if (term->text_buf[r * term->n_cols + c].glyph != ' ')
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

            new_text_buf[new_r * new_n_cols + new_c] = term->text_buf[r * term->n_cols + c];
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

    term_refresh(term);
    return term;
}