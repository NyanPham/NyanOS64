#ifndef TERMINAL_H
#define TERMINAL_H

#include "window.h"
#include "kern_defs.h"
#include "utils/ring_buf.h"

#include <stddef.h>
#include <stdint.h>

#define CURSOR_ENABLED (1 << 0)
#define CURSOR_STATE (1 << 1)
#define TERM_DIRTY (1U << 31)

typedef struct Terminal
{
    // VIEW
    Window *win; // A terminal has a window to draw into

    // DATA MODEL
    TermCell *text_buf; // For scroll up and down
    uint64_t text_buf_siz;
    uint64_t n_rows;
    uint64_t n_cols;

    // STATE
    uint64_t start_line_idx;
    uint64_t scroll_idx;
    uint64_t cur_row;
    uint64_t cur_col;
    int32_t dirty_start_row_idx;
    int32_t dirty_end_row_idx;

    // CONTROLLER (Shell)
    int child_pid;

    AnsiContext ansi_ctx;

    // TODO: use input_buf store the pressed keys from keyboard
    // when see KEY_PRESSED event, instead of
    // reading from keyboard buf directly
    // INPUT BUFFER
    RingBuf input_buf;
    int waiting_pid;

    uint32_t flags;
} Terminal;

Terminal *term_create(int64_t x, int64_t y, uint64_t w, uint64_t h, uint64_t max_rows, const char *title, uint32_t win_flags);
void term_destroy(Terminal *term);
void term_refresh(Terminal *term);
void term_put_char(Terminal *term, char c);
size_t term_read(Terminal *term, char *buf, size_t count);
void term_scroll(Terminal *term, int32_t delta);
Terminal *term_resize(Terminal *term, uint64_t w, uint64_t h);
void term_blink_active(void);
void term_paint(void);
static Terminal *get_curr_term(void);

#endif