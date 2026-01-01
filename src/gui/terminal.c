#include "terminal.h"
#include "mem/kmalloc.h"
#include "mem/vmm.h"
#include "sched/sched.h"
#include "drivers/keyboard.h"
#include "../string.h"
#include "kern_defs.h"

/**
 * @brief 
 * x, y, w, and h are pixel values for the window (view)
 * 
 * First, crates the View (window)
 * Then initializes the Data Model like n_rows, n_cols
 */
Terminal* term_create(int64_t x, int64_t y, uint64_t w, uint64_t h, uint64_t max_rows)
{
    // View
    Terminal* term = (Terminal*)kmalloc(sizeof(Terminal));
    term->win = create_win(x, y, w, h, "Terminal");
    
    // Data Model
    term->n_rows = max_rows;
    term->n_cols = w / CHAR_W; // cols num is based on the width of the window 

    uint64_t buf_size = term->n_rows * term->n_cols * sizeof(TermCell);
    term->text_buf = (TermCell*)vmm_alloc(buf_size);
    if (term->text_buf == NULL)
    {
        // PANIC
        kprint("Panic: term_create cannot vmm_alloc for text_buf\n");
        return NULL;
    }
    
    memset(term->text_buf, 0, buf_size);

    // State
    term->scroll_idx = 0;
    term->cur_row = 0;
    term->cur_col = 0;
    term->child_pid = -1;

    rb_init(&term->input_buf);

    return term;
}

/**
 * @brief Renders by scanning the buffer and paint on window
 */
void term_refresh(Terminal* term)
{
    // viewport is identified with scroll_idx

    uint64_t win_rows = term->win->height / CHAR_H;

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
            uint32_t color = (cell.color == 0) ? White : cell.color;
            
            win_draw_char_at(term->win, ch, px + WIN_BORDER_SIZE, py + WIN_TITLE_BAR_H, color);
        }
    }
}

void term_put_char(Terminal* term, char c)
{
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
        }
    }
    else 
    {
        // record into the buffer
        uint64_t idx = term->cur_row * term->n_cols + term->cur_col;
        if (idx < term->n_rows * term->n_cols)
        {
            term->text_buf[idx].glyph = c;
            term->text_buf[idx].color = White;
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
        TermCell* dst = term->text_buf;
        TermCell* src = term->text_buf + term->n_cols;
        uint64_t count = (term->n_rows - 1) * term->n_cols * sizeof(TermCell);

        memcpy(dst, src, count);

        TermCell* last_row = term->text_buf + (term->n_rows - 1) * term->n_cols;
        memset(last_row, 0, term->n_cols * sizeof(TermCell));

        term->cur_row = term->n_rows - 1;
    }

    // handle the scroll in the view
    uint64_t win_rows = (term->win->height - WIN_TITLE_BAR_H) / CHAR_H;
    if (term->cur_row >= term->scroll_idx + win_rows)
    {
        term->scroll_idx = term->cur_row - win_rows + 1;
    }

    term_refresh(term);
}

size_t term_read(Terminal* term, char* buf, size_t count)
{
    size_t n = 0;
    while (n < count)
    {
        char c = keyboard_get_char();
        if (c != 0)
        {
            // store to buf 
            buf[n++] = c;

            // could be echoed to the screen
            // BUT, we'll let the task that owns 
            // the terminal to decide what to print
            // instead :))
            // term_put_char(term, c);

            if (c  == '\n')
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