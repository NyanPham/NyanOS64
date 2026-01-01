#ifndef TERMINAL_H
#define TERMINAL_H

#include "window.h"
#include "kern_defs.h"
#include "utils/ring_buf.h"

#include <stddef.h>
#include <stdint.h>

typedef struct Terminal
{   
    // VIEW
    Window* win; // A terminal has a window to draw into

    // DATA MODEL
    TermCell* text_buf; // For scroll up and down
    uint64_t n_rows;
    uint64_t n_cols;

    // STATE
    uint64_t scroll_idx;
    uint64_t cur_row;
    uint64_t cur_col;

    // CONTROLLER (Shell)
    int child_pid;

    AnsiContext ansi_ctx;

    // INPUT BUFFER
    RingBuf input_buf;
    int waiting_pid;
} Terminal;  

Terminal* term_create(int64_t x, int64_t y, uint64_t w, uint64_t h, uint64_t max_rows);
void term_refresh(Terminal* term);
void term_put_char(Terminal* term, char c);
size_t term_read(Terminal* term, char* buf, size_t count);

#endif