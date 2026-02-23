#include "libc/libc.h"
#include "include/ansi.h"
#include "include/color.h"
#include "include/font.h"

#define CHAR_W 8
#define CHAR_H 8

typedef struct
{
    char glyph;
    uint32_t fg;
    uint32_t bg;
} TermCell;

TermCell *text_buf = NULL;
uint32_t *frame_buf = NULL;

int n_rows = 0;  // number of rows
int n_cols = 0;  // number of columns
int cur_row = 0; // cursor row
int cur_col = 0; // cursor column
int start_line_idx = 0;

uint32_t curr_fg = White;
uint32_t curr_bg = Black;

int win_w = 600;
int win_h = 400;

uint8_t cur_vis = 1;

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

static void drv_put_char(void *data, char c) { term_put_char(c); }

static void drv_set_color(void *data, uint32_t color) { curr_fg = color; }

static void drv_set_cursor(void *data, int c, int r)
{
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
    for (int r = 0; r < n_rows; r += 1)
    {
        int buf_row = (start_line_idx + r) % n_rows;
        for (int c = 0; c < n_cols; c += 1)
        {
            TermCell cell = text_buf[buf_row * n_cols + c];
            char ch = (cell.glyph == 0) ? ' ' : cell.glyph;
            render_cell_to_fb(ch, c, r, cell.fg, cell.bg);
        }
    }
    if (cur_vis)
    {
        int buf_row = (start_line_idx + cur_row) % n_rows;
        TermCell cell = text_buf[buf_row * n_cols + cur_col];
        char ch = (cell.glyph == 0) ? ' ' : cell.glyph;

        render_cell_to_fb(ch, cur_col, cur_row, Black, White);
    }
    blit(0, 0, win_w, win_h, frame_buf);
}

int main(int argc, char **argv)
{
    char *prog_path = "bin/shell.elf";
    char **prog_argv = (char *[]){"shell.elf", NULL};
    char win_title[256] = "NyanOS Terminal";

    uint32_t win_flags = WIN_MOVABLE | WIN_MINIMIZABLE | WIN_RESIZABLE;
    int arg_idx = 1;

    if (argc > 3 && strcmp(argv[1], "--fixed") == 0)
    {
        win_flags = WIN_MOVABLE | WIN_MINIMIZABLE;

        int req_cols = parse_int(argv[2]);
        int req_rows = parse_int(argv[3]);

        win_w = req_cols * CHAR_W + 16;
        win_h = req_rows * CHAR_H + 40;

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

    int ptm_pipe[2];
    int pts_pipe[2];
    if (pipe(ptm_pipe) < 0 || pipe(pts_pipe) < 0)
    {
        exit(1);
    }

    // Child: The app (shell, snake, etc.)
    int shell_pid = fork();
    if (shell_pid == 0)
    {
        close(ptm_pipe[1]);
        close(pts_pipe[0]);
        dup2(ptm_pipe[0], 0);
        dup2(pts_pipe[1], 1);
        dup2(pts_pipe[1], 2);
        close(ptm_pipe[0]);
        close(pts_pipe[1]);

        exec(prog_path, prog_argv);

        print("Command not found: ");
        print(prog_path);
        print("\n");
        exit(1);
    }

    // Parent: Check event and render
    close(ptm_pipe[0]);
    close(pts_pipe[1]);

    n_cols = win_w / CHAR_W;
    n_rows = win_h / CHAR_H;

    text_buf = malloc(n_rows * n_cols * sizeof(TermCell));
    frame_buf = malloc(win_w * win_h * sizeof(uint32_t));

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
                    int new_w = 0, new_h = 0;
                    win_get_size(&new_w, &new_h);
                    if (new_w > 0 && new_h > 0)
                    {
                        free(text_buf);
                        free(frame_buf);

                        win_w = new_w;
                        win_h = new_h;
                        n_cols = win_w / CHAR_W;
                        n_rows = win_h / CHAR_H;

                        text_buf = malloc(n_rows * n_cols * sizeof(TermCell));
                        frame_buf = malloc(win_w * win_h * sizeof(uint32_t));

                        for (int i = 0; i < n_rows * n_cols; i++)
                        {
                            text_buf[i].glyph = ' ';
                            text_buf[i].fg = curr_fg;
                            text_buf[i].bg = curr_bg;
                        }

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

                    if (esc_state == 0 && c == '\033')
                    {
                        esc_state = 1;
                    }
                    else if (esc_state == 1 && c == '[')
                    {
                        esc_state = 2;
                    }
                    else if (esc_state == 2 && c == '?')
                    {
                        esc_state = 3;
                    }
                    else if (esc_state == 3 && c == '2')
                    {
                        esc_state = 4;
                    }
                    else if (esc_state == 4 && c == '5')
                    {
                        esc_state = 5;
                    }
                    else if (esc_state == 5 && c == 'l')
                    {
                        cur_vis = 0;
                        esc_state = 0;
                    }
                    else if (esc_state == 5 && c == 'h')
                    {
                        cur_vis = 1;
                        esc_state = 0;
                    }
                    else
                    {
                        esc_state = 0;
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
                term_refresh();
            }
            else // shell exited
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