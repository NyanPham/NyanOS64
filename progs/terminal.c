#include "libc/libc.h"
#include "include/ansi.h"
#include "include/color.h"
#include "include/font.h"

typedef struct
{
    char glyph;
    uint32_t fg;
    uint32_t bg;
} TermCell;

TermCell *main_buf = NULL;     // Buf of the contents of the terminal
TermCell *alt_buf = NULL;      // Buf used for other apps that borrow the terminal, such as an editor
TermCell *text_buf = NULL;     // Buf that points to either main_buf or alt_buf
TermCell *screen_state = NULL; // Buf used to compare for the differences, to optimize rendering
uint8_t is_alt = 0;            // A flag indicating whether text_buf is pointing to main_buf or alt_buf
uint32_t *frame_buf = NULL;

// Some other states when we switches from alt_buf to main_buf
int saved_cur_row = 0;
int saved_cur_col = 0;
int saved_start_line = 0;
uint32_t saved_fg = 0;
uint32_t saved_bg = 0;

int n_rows = 0;         // Num of rows terminal has in Char
int n_cols = 0;         // Num of cols terminal has in Char
int cur_row = 0;        // Cursor row idx
int cur_col = 0;        // Cursor col idx
int start_line_idx = 0; // The line index of the start for the viewport in terminal

uint32_t curr_fg = White; // Terminal forground color
uint32_t curr_bg = Black; // Terminal bg color

int win_w = 600; // Terminal's window width in pixels
int win_h = 400; // Terminal's window height in pixels

uint8_t cur_vis = 1;

// Parse string of decimal into int value
int parse_int(char *str)
{
    int res = 0;
    for (int i = 0; str[i] >= '0' && str[i] <= '9'; i++)
    {
        res = res * 10 + (str[i] - '0');
    }
    return res;
}

void term_scroll()
{
    for (int c = 0; c < n_cols; c += 1)
    {
        int idx = start_line_idx * n_cols + c;
        text_buf[idx].glyph = ' ';
        text_buf[idx].fg = curr_fg;
        text_buf[idx].bg = curr_bg;
    }
    start_line_idx = (start_line_idx + 1) % n_rows;
    cur_row -= 1;
}

void term_put_char(char c)
{
    if (c == '\n')
    {
        cur_col = 0;
        cur_row += 1;
    }
    else if (c == '\b')
    {
        if (cur_col > 0)
        {
            cur_col -= 1;
            int idx = ((start_line_idx + cur_row) % n_rows) * n_cols + cur_col;

            text_buf[idx].glyph = ' ';
            text_buf[idx].fg = curr_fg;
            text_buf[idx].bg = curr_bg;
        }
    }
    else
    {
        int idx = ((start_line_idx + cur_row) % n_rows) * n_cols + cur_col;

        text_buf[idx].glyph = c;
        text_buf[idx].fg = curr_fg;
        text_buf[idx].bg = curr_bg;

        cur_col += 1;
        if (cur_col >= n_cols)
        {
            cur_col = 0;
            cur_row += 1;
        }
    }
    if (cur_row >= n_rows)
        term_scroll();
}

// =========================================================
// ANSI DRIVER
// =========================================================

static void drv_put_char(void *data, char c)
{
    (void)data;
    term_put_char(c);
}

static void drv_set_color(void *data, uint32_t color)
{
    (void)data;
    curr_fg = color;
}

static void drv_set_cursor(void *data, int c, int r)
{
    (void)data;
    cur_col = c;
    cur_row = r;

    // Bounds checking
    if (cur_col >= n_cols)
    {
        cur_col = n_cols - 1;
    }
    if (cur_row >= n_rows)
    {
        cur_row = n_rows - 1;
    }
}

static void drv_clear_screen(void *data, int mode)
{
    (void)data;
    if (mode == 2)
    {
        for (int i = 0; i < n_rows * n_cols; i += 1)
        {
            text_buf[i].glyph = ' ';
            text_buf[i].fg = curr_fg;
            text_buf[i].bg = curr_bg;
        }
        cur_col = 0;
        cur_row = 0;
        start_line_idx = 0;
    }
}

static const AnsiDriver term_driver = {
    .put_char = drv_put_char,
    .set_color = drv_set_color,
    .set_cursor = drv_set_cursor,
    .clear_screen = drv_clear_screen,
    .scroll = NULL,
    .set_mode = NULL};

// =========================================================

void render_cell_to_fb(char c, int col, int row, uint32_t fg, uint32_t bg)
{
    int px = col * CHAR_W;
    int py = row * CHAR_H;
    if (px >= win_w || py >= win_h)
    {
        return;
    }

    char *glyph = font8x8_basic[(int)c];
    for (int dy = 0; dy < 8; dy += 1)
    {
        for (int dx = 0; dx < 8; dx += 1)
        {
            uint32_t color = ((glyph[dy] >> dx) & 1) ? fg : bg;
            frame_buf[(py + dy) * win_w + (px + dx)] = color;
        }
    }
}

void term_refresh()
{
    if (!screen_state)
    {
        return;
    }

    uint8_t is_dirty = 0;
    for (int r = 0; r < n_rows; r += 1)
    {
        int buf_row = (start_line_idx + r) % n_rows;
        for (int c = 0; c < n_cols; c += 1)
        {
            TermCell cell = text_buf[buf_row * n_cols + c];
            TermCell target = cell;

            // if the curr pos is cursor, we want to
            // switch the fg anf bg to highlight it
            if (cur_vis && (r == cur_row) && (c == cur_col))
            {
                target.fg = Black;
                target.bg = White;
            }

            // we compare the target with the previous screen states
            // if change -> render

            TermCell *onscreen = &screen_state[r * n_cols + c];
            if (target.glyph != onscreen->glyph || target.fg != onscreen->fg || target.bg != onscreen->bg)
            {
                char ch = (target.glyph == 0) ? ' ' : target.glyph;
                render_cell_to_fb(ch, c, r, target.fg, target.bg);
                *onscreen = target;
                is_dirty = 1;
            }
        }
    }

    if (is_dirty)
    {
        blit(0, 0, win_w, win_h, frame_buf);
    }
}

int main(int argc, char **argv)
{
    char *prog_path = "/bin/shell.elf";
    char **prog_argv = (char *[]){"shell.elf", NULL};
    char win_title[256] = "NyanOS Terminal";

    uint32_t win_flags = WIN_MOVABLE | WIN_MINIMIZABLE | WIN_RESIZABLE;
    int arg_idx = 1;

    if (argc > 3 && strcmp(argv[1], "--fixed") == 0)
    {
        win_flags = WIN_MOVABLE | WIN_MINIMIZABLE;

        int req_cols = parse_int(argv[2]);
        int req_rows = parse_int(argv[3]);

        win_w = req_cols * CHAR_W;
        win_h = req_rows * CHAR_H;

        arg_idx = 4;
    }

    if (argc > arg_idx)
    {
        prog_path = argv[arg_idx];
        prog_argv = &argv[arg_idx];
        strcpy(win_title, prog_path);
    }

    WinParams_t wp;
    wp.x = 100;
    wp.y = 100;
    wp.width = win_w;
    wp.height = win_h;
    strcpy(wp.title, win_title);

    wp.flags = win_flags;

    if (win_create(&wp) < 0)
    {
        return -1;
    }

    int ptm_pipe[2]; // parent to master (term writes in, shell reads out)
    int pts_pipe[2]; // parent to slave (term reads in, shell writes in)
    if (pipe(ptm_pipe) < 0 || pipe(pts_pipe) < 0)
    {
        exit(1);
    }

    // Child: can be shell, or game (snake)
    int shell_pid = fork();
    if (shell_pid == 0)
    {
        close(ptm_pipe[1]);   // this pipe is to receive from parent, just read, close write
        close(pts_pipe[0]);   // this pipe is to send to parent, just write, close read
        dup2(ptm_pipe[0], 0); // replace shell's stdin with term's write
        dup2(pts_pipe[1], 1); // replace shell's stdout with term's read
        dup2(pts_pipe[1], 2); // replace shell's stderr with term's read
        close(ptm_pipe[0]);
        close(pts_pipe[1]);

        exec(prog_path, prog_argv);

        print("Command not found: ");
        print(prog_path);
        print("\n");
        exit(1);
    }

    close(ptm_pipe[0]); // this pipe is to send to shell, just write, close read
    close(pts_pipe[1]); // this pipe is to receive from shell, just read, close wrie

    n_cols = win_w / CHAR_W;
    n_rows = win_h / CHAR_H;

    main_buf = malloc(n_rows * n_cols * sizeof(TermCell));
    alt_buf = malloc(n_rows * n_cols * sizeof(TermCell));
    frame_buf = malloc(win_w * win_h * sizeof(uint32_t));

    memset(frame_buf, 0, win_w * win_h * sizeof(uint32_t));

    screen_state = malloc(n_rows * n_cols * sizeof(TermCell));
    for (int i = 0; i < n_rows * n_cols; i++)
    {
        screen_state[i].glyph = 0xFF;
        screen_state[i].fg = 0;
        screen_state[i].bg = 0;
    }

    text_buf = main_buf; // by default, terminal shows its own contents
    is_alt = 0;

    for (int i = 0; i < n_rows * n_cols; i += 1)
    {
        main_buf[i].glyph = ' ';
        main_buf[i].fg = curr_fg;
        main_buf[i].bg = curr_bg;
        alt_buf[i].glyph = ' ';
        alt_buf[i].fg = curr_fg;
        alt_buf[i].bg = curr_bg;
    }

    for (int i = 0; i < n_rows * n_cols; i += 1)
    {
        text_buf[i].glyph = ' ';
        text_buf[i].fg = curr_fg;
        text_buf[i].bg = curr_bg;
    }

    AnsiContext ansi_ctx;
    ansi_ctx.color = curr_fg;
    ansi_ctx.state = ANSI_NORMAL;
    ansi_ctx.idx = 0;
    memset(ansi_ctx.buf, 0, ANSI_BUF_SIZE);
    term_refresh();

    int watch_fds[1] = {pts_pipe[0]};
    int num_watch = 1;
    uint8_t shell_dead = 0;
    char buf[256];
    Event e;

    while (1)
    {
        int mask = await_io(watch_fds, num_watch, 1, 0);
        if (mask < 0)
        {
            exit(1);
        }

        // Handle events: click, resize, etc.
        if (mask & 1)
        {
            while (get_event(&e, O_NONBLOCK) > 0)
            {
                if (e.type == EVENT_KEY_PRESSED)
                {
                    if (shell_dead)
                    {
                        exit(0);
                    }
                    char c = e.key;
                    uint8_t is_ctrl = e.modifiers & MOD_CTRL;
                    if (c == 'c' && is_ctrl)
                    {
                        if (kill_fg(shell_pid) == 0)
                        {
                            char etx = '\x03';
                            write(ptm_pipe[1], &etx, 1);
                        }
                    }
                    else
                    {
                        if (is_ctrl)
                        {
                            c &= 0x1F;
                        }

                        if (write(ptm_pipe[1], &c, 1) <= 0)
                        {
                            exit(0);
                        }
                    }
                }
                else if (e.type == EVENT_WIN_RESIZE)
                {
                    int new_w = 0;
                    int new_h = 0;
                    win_get_size(&new_w, &new_h);

                    if (new_w > 0 && new_h > 0)
                    {
                        if (main_buf)
                        {
                            free(main_buf);
                        }
                        if (alt_buf)
                        {
                            free(alt_buf);
                        }
                        if (frame_buf)
                        {
                            free(frame_buf);
                        }
                        if (screen_state)
                        {
                            free(screen_state);
                        }

                        win_w = new_w;
                        win_h = new_h;
                        n_cols = win_w / CHAR_W;
                        n_rows = win_h / CHAR_H;

                        main_buf = malloc(n_rows * n_cols * sizeof(TermCell));
                        alt_buf = malloc(n_rows * n_cols * sizeof(TermCell));
                        frame_buf = malloc(win_w * win_h * sizeof(uint32_t));
                        screen_state = malloc(n_rows * n_cols * sizeof(TermCell));

                        memset(frame_buf, 0, win_w * win_h * sizeof(uint32_t));

                        for (int i = 0; i < n_rows * n_cols; i++)
                        {
                            screen_state[i].glyph = 0xFF;
                            screen_state[i].fg = 0;
                            screen_state[i].bg = 0;

                            main_buf[i].glyph = ' ';
                            main_buf[i].fg = curr_fg;
                            main_buf[i].bg = curr_bg;

                            alt_buf[i].glyph = ' ';
                            alt_buf[i].fg = curr_fg;
                            alt_buf[i].bg = curr_bg;
                        }
                        text_buf = is_alt ? alt_buf : main_buf;

                        cur_col = 0;
                        cur_row = 0;
                        start_line_idx = 0;
                        term_refresh();

                        if (!shell_dead)
                        {
                            char ctrl_l = '\x0C';
                            write(ptm_pipe[1], &ctrl_l, 1);
                        }
                    }
                }
            }
        }

        // Render
        if (mask & 2)
        {
            int n = read(pts_pipe[0], buf, 255);
            if (n > 0)
            {
                static int esc_state = 0;
                for (int i = 0; i < n; i += 1)
                {
                    char c = buf[i];
                    uint8_t intercepted = 0;

                    if (esc_state == 0 && c == '\033')
                    {
                        esc_state = 1;
                    }
                    else if (esc_state == 1 && c == '[')
                    {
                        esc_state = 2;
                    }
                    else if (esc_state == 2 && c == 'q')
                    {
                        char term_dim_rep[4] = {
                            '\033',
                            (char)n_cols,
                            (char)n_rows,
                            'Q'};
                        write(ptm_pipe[1], term_dim_rep, 4);
                        esc_state = 0;
                        ansi_ctx.state = ANSI_NORMAL;
                        ansi_ctx.idx = 0;
                        continue;
                    }
                    else if (esc_state == 2 && c == '?')
                    {
                        esc_state = 3;
                        intercepted = 1;
                    }
                    else if (esc_state == 3 && c == '2')
                    {
                        esc_state = 4;
                        intercepted = 1;
                    }
                    else if (esc_state == 4 && c == '5')
                    {
                        esc_state = 5;
                        intercepted = 1;
                    }
                    else if (esc_state == 5 && c == 'l')
                    {
                        cur_vis = 0;
                        esc_state = 0;
                        ansi_ctx.state = ANSI_NORMAL;
                        ansi_ctx.idx = 0;
                        intercepted = 1;
                    }
                    else if (esc_state == 5 && c == 'h')
                    {
                        cur_vis = 1;
                        esc_state = 0;
                        ansi_ctx.state = ANSI_NORMAL;
                        ansi_ctx.idx = 0;
                        intercepted = 1;
                    }
                    else if (esc_state == 3 && c == '4')
                    {
                        esc_state = 6;
                        intercepted = 1;
                    }
                    else if (esc_state == 6 && c == '7')
                    {
                        esc_state = 7;
                        intercepted = 1;
                    }
                    else if (esc_state == 7 && c == 'h')
                    {
                        is_alt = 1;
                        text_buf = alt_buf;

                        saved_cur_row = cur_row;
                        saved_cur_col = cur_col;
                        saved_start_line = start_line_idx;
                        saved_fg = curr_fg;
                        saved_bg = curr_bg;

                        for (int j = 0; j < n_rows * n_cols; j++)
                        {
                            text_buf[j].glyph = ' ';
                            text_buf[j].fg = curr_fg;
                            text_buf[j].bg = curr_bg;
                        }
                        cur_col = 0;
                        cur_row = 0;
                        start_line_idx = 0;
                        esc_state = 0;
                        ansi_ctx.state = ANSI_NORMAL;
                        ansi_ctx.idx = 0;
                        intercepted = 1;
                    }
                    else if (esc_state == 7 && c == 'l')
                    {
                        is_alt = 0;
                        text_buf = main_buf;

                        cur_row = saved_cur_row;
                        cur_col = saved_cur_col;
                        start_line_idx = saved_start_line;
                        curr_fg = saved_fg;
                        curr_bg = saved_bg;

                        if (screen_state)
                        {
                            for (int j = 0; j < n_rows * n_cols; j++)
                            {
                                screen_state[j].glyph = 0xFF;
                            }
                        }

                        esc_state = 0;
                        ansi_ctx.state = ANSI_NORMAL;
                        ansi_ctx.idx = 0;
                        intercepted = 1;
                    }
                    else
                    {
                        esc_state = 0;
                    }

                    if (intercepted)
                    {
                        continue;
                    }

                    if (c == '\x04')
                    {
                        shell_dead = 1;
                        char *msg = "\n\n[Shell closed. Press any key to exit.]";
                        for (int j = 0; msg[j] != '\0'; j++)
                        {
                            ansi_write_char(&ansi_ctx, msg[j], &term_driver, NULL);
                        }

                        close(pts_pipe[0]);
                        num_watch = 0;
                        break;
                    }

                    ansi_write_char(&ansi_ctx, buf[i], &term_driver, NULL);
                }
                int check_fds[1] = {pts_pipe[0]};
                int next_mask = await_io(check_fds, 1, 0, O_NONBLOCK);
                if ((next_mask & 2) == 0)
                {
                    term_refresh();
                }
            }
            else
            {
                if (!shell_dead)
                {
                    shell_dead = 1;
                    char *msg = "\n\n[Shell closed. Press any key to exit.]";
                    for (int j = 0; msg[j] != '\0'; j++)
                    {
                        ansi_write_char(&ansi_ctx, msg[j], &term_driver, NULL);
                    }
                    term_refresh();
                    close(pts_pipe[0]);
                    num_watch = 0;
                }
            }
        }
    }
    return 0;
}