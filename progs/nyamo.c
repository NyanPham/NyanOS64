#include "libc/libc.h"

typedef struct
{
    char *chars;
    int len;
    int cap;
} ERow;

ERow *rows = NULL;
int num_lines = 0;
int rows_cap = 0;

int cur_x = 0;
int cur_y = 0;
int row_offset = 0; // the row to the top of the viewport -> vertical scrolling
int col_offset = 0; // the col to the left of the viewport -> horizontal scrolling
char filepath[128];

int screen_rows = 50;
int screen_cols = 75;

char *mega_buf = NULL; // accums drawing commands to 1 buf to avoid several calling to sys_write
int mega_cap = 0;
uint8_t soft_wrap = 0;

uint8_t need_full_redraw = 1;

// Overhead for each line. Each might contain some other commands line \033[24;80H.
// 32 is a safe number to account for them.
#define ROW_OVERHEAD 0x20

// Overhead for the whole screen. At the beginning or the end, there might be some
// other commands to clear screen, hide/show cursors.
#define SAFETY_OVERHEAD 0x400

void ensure_mega_buf()
{
    int needed = screen_rows * (screen_cols + ROW_OVERHEAD) + SAFETY_OVERHEAD;
    if (mega_cap < needed)
    {
        if (mega_buf)
        {
            free(mega_buf);
        }
        mega_buf = malloc(needed);
        mega_cap = needed;
    }
}

void row_insert(int at, const char *s, int len)
{
    if (at < 0 || at > num_lines)
    {
        return;
    }

    if (num_lines >= rows_cap)
    {
        rows_cap = (rows_cap == 0) ? ROW_OVERHEAD : rows_cap * 2;
        rows = realloc(rows, rows_cap * sizeof(ERow));
    }

    for (int i = num_lines; i > at; i--)
    {
        rows[i] = rows[i - 1];
    }

    rows[at].len = len;
    rows[at].cap = len + ROW_OVERHEAD;
    rows[at].chars = malloc(rows[at].cap);
    if (len > 0)
    {
        memcpy(rows[at].chars, s, len);
    }
    rows[at].chars[len] = '\0';
    num_lines++;
}

void row_free(ERow *row)
{
    free(row->chars);
}

void row_delete(int at)
{
    if (at < 0 || at >= num_lines)
    {
        return;
    }

    row_free(&rows[at]);

    for (int i = at; i < num_lines - 1; i++)
    {
        rows[i] = rows[i + 1];
    }

    num_lines--;
}

void row_insert_char(ERow *row, int at, char c)
{
    if (at < 0 || at > row->len)
    {
        at = row->len;
    }

    if (row->len + 1 >= row->cap)
    {
        row->cap *= 2;
        row->chars = realloc(row->chars, row->cap);
    }

    memmove(&row->chars[at + 1], &row->chars[at], row->len - at + 1);
    row->chars[at] = c;
    row->len++;
}

void row_del_char(ERow *row, int at)
{
    if (at < 0 || at >= row->len)
    {
        return;
    }
    memmove(&row->chars[at], &row->chars[at + 1], row->len - at);
    row->len--;
}

void row_append_str(ERow *row, char *s, int len)
{
    if (row->len + len + 1 >= row->cap)
    {
        row->cap = row->len + len + 32;
        row->chars = realloc(row->chars, row->cap);
    }
    memcpy(&row->chars[row->len], s, len);
    row->len += len;
    row->chars[row->len] = '\0';
}

void append_int(char *buf, int *idx, int val)
{
    if (val == 0)
    {
        buf[(*idx)++] = '0';
        return;
    }
    char rev[16];
    int r = 0;
    while (val > 0)
    {
        rev[r++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = r - 1; i >= 0; i--)
    {
        buf[(*idx)++] = rev[i];
    }
}

void append_move_cursor(char *buf, int *idx, int r, int c)
{
    buf[(*idx)++] = '\033';
    buf[(*idx)++] = '[';
    append_int(buf, idx, r);
    buf[(*idx)++] = ';';
    append_int(buf, idx, c);
    buf[(*idx)++] = 'H';
}

void edtr_drw()
{
    ensure_mega_buf();
    int m_idx = 0;
    int max_w = screen_cols - 1;

    char *wrap_str = soft_wrap ? " | [Ctrl+Z] Wrap: ON " : " | [Ctrl+Z] Wrap: OFF ";
    char *chunks[] = {" Nyamo ", " | [Ctrl+S] Save ", " | [Ctrl+Q] Quit ", wrap_str};
    int num_chunks = 4;

    int stat_bar_rows = 1;
    int curr_col_usage = 0;
    for (int i = 0; i < num_chunks; i++)
    {
        int len = strlen(chunks[i]);
        if (curr_col_usage + len > max_w && curr_col_usage > 0)
        {
            stat_bar_rows++;
            curr_col_usage = len;
        }
        else
        {
            curr_col_usage += len;
        }
    }

    int text_area_rows = screen_rows - stat_bar_rows - 1;
    if (text_area_rows < 1)
    {
        text_area_rows = 1;
    }

    memcpy(&mega_buf[m_idx], "\033[?25l", 6);
    m_idx += 6;
    memcpy(&mega_buf[m_idx], "\033[0m", 4);
    m_idx += 4;

    int display_cx = 0;
    int display_cy = 0;

    if (soft_wrap)
    {
        int cursor_vy = 0;
        for (int i = 0; i < cur_y; i++)
        {
            cursor_vy += (rows[i].len / max_w) + 1;
        }
        cursor_vy += (cur_x / max_w);
        int cursor_vx = cur_x % max_w;

        int old_r_off = row_offset;
        if (cursor_vy < row_offset)
        {
            row_offset = cursor_vy;
        }
        if (cursor_vy >= row_offset + text_area_rows)
        {
            row_offset = cursor_vy - text_area_rows + 1;
        }

        if (old_r_off != row_offset)
        {
            need_full_redraw = 1;
        }

        if (need_full_redraw)
        {
            int current_vy = 0;
            int drawn_lines = 0;

            for (int i = 0; i < num_lines && drawn_lines < text_area_rows; i++)
            {
                int v_lines = (rows[i].len / max_w) + 1;
                if (current_vy + v_lines <= row_offset)
                {
                    current_vy += v_lines;
                    continue;
                }

                for (int v = 0; v < v_lines; v++)
                {
                    int this_vy = current_vy + v;
                    if (this_vy >= row_offset && this_vy < row_offset + text_area_rows)
                    {
                        append_move_cursor(mega_buf, &m_idx, drawn_lines + 1, 1);
                        int char_start = v * max_w;
                        int char_len = rows[i].len - char_start;
                        if (char_len > max_w)
                        {
                            char_len = max_w;
                        }

                        if (char_len > 0)
                        {
                            memcpy(&mega_buf[m_idx], &rows[i].chars[char_start], char_len);
                            m_idx += char_len;
                        }

                        int fill = max_w - char_len;
                        if (fill > 0)
                        {
                            memset(&mega_buf[m_idx], ' ', fill);
                            m_idx += fill;
                        }
                        drawn_lines++;
                    }
                }
                current_vy += v_lines;
            }

            while (drawn_lines < text_area_rows)
            {
                append_move_cursor(mega_buf, &m_idx, drawn_lines + 1, 1);
                memset(&mega_buf[m_idx], ' ', max_w);
                m_idx += max_w;
                drawn_lines++;
            }
            need_full_redraw = 0;
        }
        else
        {
            int current_vy = 0;
            for (int i = 0; i < cur_y; i++)
            {
                current_vy += (rows[i].len / max_w) + 1;
            }

            int v_lines = (rows[cur_y].len / max_w) + 1;
            for (int v = 0; v < v_lines; v++)
            {
                int this_vy = current_vy + v;
                if (this_vy >= row_offset && this_vy < row_offset + text_area_rows)
                {
                    int screen_y = this_vy - row_offset;
                    append_move_cursor(mega_buf, &m_idx, screen_y + 1, 1);

                    int char_start = v * max_w;
                    int char_len = rows[cur_y].len - char_start;
                    if (char_len > max_w)
                    {
                        char_len = max_w;
                    }

                    if (char_len > 0)
                    {
                        memcpy(&mega_buf[m_idx], &rows[cur_y].chars[char_start], char_len);
                        m_idx += char_len;
                    }
                    int fill = max_w - char_len;
                    if (fill > 0)
                    {
                        memset(&mega_buf[m_idx], ' ', fill);
                        m_idx += fill;
                    }
                }
            }
        }

        display_cy = cursor_vy - row_offset;
        display_cx = cursor_vx;
    }
    else
    {
        int old_r_off = row_offset;
        int old_c_off = col_offset;

        if (cur_y < row_offset)
        {
            row_offset = cur_y;
        }
        if (cur_y >= row_offset + text_area_rows)
        {
            row_offset = cur_y - text_area_rows + 1;
        }

        if (cur_x < col_offset)
        {
            col_offset = cur_x;
        }
        if (cur_x >= col_offset + max_w)
        {
            col_offset = cur_x - max_w + 1;
        }

        if (old_r_off != row_offset || old_c_off != col_offset)
        {
            need_full_redraw = 1;
        }

        if (need_full_redraw)
        {
            for (int i = 0; i < text_area_rows; i++)
            {
                int file_row = row_offset + i;
                append_move_cursor(mega_buf, &m_idx, i + 1, 1);

                if (file_row < num_lines)
                {
                    int len = rows[file_row].len;
                    int visible_len = 0;
                    if (len > col_offset)
                    {
                        visible_len = len - col_offset;
                        if (visible_len > max_w)
                        {
                            visible_len = max_w;
                        }
                        memcpy(&mega_buf[m_idx], rows[file_row].chars + col_offset, visible_len);
                        m_idx += visible_len;
                    }
                    int fill = max_w - visible_len;
                    if (fill > 0)
                    {
                        memset(&mega_buf[m_idx], ' ', fill);
                        m_idx += fill;
                    }
                }
                else
                {
                    memset(&mega_buf[m_idx], ' ', max_w);
                    m_idx += max_w;
                }
            }
            need_full_redraw = 0;
        }
        else
        {
            int screen_y = cur_y - row_offset;
            append_move_cursor(mega_buf, &m_idx, screen_y + 1, 1);

            int len = rows[cur_y].len;
            int visible_len = 0;
            if (len > col_offset)
            {
                visible_len = len - col_offset;
                if (visible_len > max_w)
                {
                    visible_len = max_w;
                }
                memcpy(&mega_buf[m_idx], rows[cur_y].chars + col_offset, visible_len);
                m_idx += visible_len;
            }
            int fill = max_w - visible_len;
            if (fill > 0)
            {
                memset(&mega_buf[m_idx], ' ', fill);
                m_idx += fill;
            }
        }

        display_cy = cur_y - row_offset;
        display_cx = cur_x - col_offset;
    }

    append_move_cursor(mega_buf, &m_idx, text_area_rows + 1, 1);
    memset(&mega_buf[m_idx], '-', max_w);
    m_idx += max_w;

    memcpy(&mega_buf[m_idx], "\033[7m", 4);
    m_idx += 4;

    int current_r = text_area_rows + 2;
    append_move_cursor(mega_buf, &m_idx, current_r, 1);
    curr_col_usage = 0;

    for (int i = 0; i < num_chunks; i++)
    {
        int len = strlen(chunks[i]);
        if (curr_col_usage + len > max_w && curr_col_usage > 0)
        {
            int fill = max_w - curr_col_usage;
            if (fill > 0)
            {
                memset(&mega_buf[m_idx], ' ', fill);
                m_idx += fill;
            }
            current_r++;
            append_move_cursor(mega_buf, &m_idx, current_r, 1);
            curr_col_usage = 0;
        }

        int print_len = len;
        if (curr_col_usage + print_len > max_w)
        {
            print_len = max_w - curr_col_usage; // Cắt cụt nếu 1 chunk quá dài
        }

        if (print_len > 0)
        {
            memcpy(&mega_buf[m_idx], chunks[i], print_len);
            m_idx += print_len;
            curr_col_usage += print_len;
        }
    }

    int final_fill = max_w - curr_col_usage;
    if (final_fill > 0)
    {
        memset(&mega_buf[m_idx], ' ', final_fill);
        m_idx += final_fill;
    }

    memcpy(&mega_buf[m_idx], "\033[0m", 4);
    m_idx += 4;

    append_move_cursor(mega_buf, &m_idx, display_cy + 1, display_cx + 1);
    memcpy(&mega_buf[m_idx], "\033[?25h", 6);
    m_idx += 6;

    mega_buf[m_idx] = '\0';

    int total_len = m_idx;
    int written = 0;
    while (written < total_len)
    {
        int chunk = total_len - written;
        if (chunk > 256)
        {
            chunk = 256;
        }
        write(1, &mega_buf[written], chunk);
        written += chunk;
    }
}

void edtr_ld()
{
    int fd = open(filepath, O_RDONLY);
    if (fd < 0)
    {
        row_insert(0, "", 0);
        return;
    }
    char buf[512];
    int n, line_len = 0;
    char line_buf[1024];
    while ((n = read(fd, buf, 512)) > 0)
    {
        for (int i = 0; i < n; i++)
        {
            if (buf[i] == '\n')
            {
                row_insert(num_lines, line_buf, line_len);
                line_len = 0;
            }
            else if (line_len < 1023)
            {
                line_buf[line_len++] = buf[i];
            }
        }
    }
    row_insert(num_lines, line_buf, line_len);
    close(fd);
    if (num_lines > 0)
    {
        cur_y = num_lines - 1;
        cur_x = rows[cur_y].len;
    }
    need_full_redraw = 1;
}

void edtr_sv()
{
    unlink(filepath);
    int fd = open(filepath, O_WRONLY | O_CREAT);
    if (fd < 0)
    {
        return;
    }
    for (int i = 0; i < num_lines; i++)
    {
        write(fd, rows[i].chars, rows[i].len);
        if (i < num_lines - 1)
        {
            write(fd, "\n", 1);
        }
    }
    close(fd);
}

void editor_query_size()
{
    int fds[1] = {0};
    while (1)
    {
        int mask = await_io(fds, 1, 0, 1);
        if (mask & 2)
        {
            char dump;
            read(0, &dump, 1);
        }
        else
        {
            break;
        }
    }

    print("\033[q");
    char reply[4];
    int r_idx = 0;
    int max_retries = 200;

    while (max_retries-- > 0 && r_idx < 4)
    {
        int ready_mask = await_io(fds, 1, 0, 1);
        if (ready_mask & 2)
        {
            char temp;
            if (read(0, &temp, 1) > 0)
            {
                if (r_idx == 0 && temp != '\033')
                {
                    continue;
                }
                reply[r_idx++] = temp;
            }
        }
        else
        {
            sleep(5);
        }
    }

    if (r_idx == 4 && reply[0] == '\033' && reply[3] == 'Q')
    {
        screen_cols = (unsigned char)reply[1];
        screen_rows = (unsigned char)reply[2];
    }
    if (screen_cols < 10)
    {
        screen_cols = 10;
    }
    if (screen_rows < 10)
    {
        screen_rows = 10;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        print("Usage: nyamo <filename>\n");
        exit(1);
    }
    strcpy(filepath, argv[1]);

    print("\033[?47h");
    editor_query_size();
    edtr_ld();

    if (num_lines == 0)
    {
        rows = malloc(sizeof(ERow));
        rows[0].chars = malloc(1);
        rows[0].chars[0] = '\0';
        rows[0].len = 0;
        rows[0].cap = 1;
        num_lines = 1;
    }

    while (1)
    {
        edtr_drw();

        char c;
        if (read(0, &c, 1) <= 0)
        {
            continue;
        }

        if (c == '\x11') // ctrl + q
        {
            break;
        }
        else if (c == '\x13') // ctrl + s
        {
            edtr_sv();
        }
        else if (c == '\x0C') // ctrl + l
        {
            editor_query_size();
            need_full_redraw = 1;
        }
        else if (c == '\x1A') // ctrl + z
        {
            soft_wrap = !soft_wrap;
            row_offset = soft_wrap ? 0 : cur_y;
            col_offset = 0;
            need_full_redraw = 1;
        }
        else if (c == '\b' || c == 0x7F)
        {
            if (cur_x > 0)
            {
                int max_w = screen_cols - 1;
                int old_v_lines = (rows[cur_y].len / max_w) + 1;

                row_del_char(&rows[cur_y], cur_x - 1);
                cur_x--;

                int new_v_lines = (rows[cur_y].len / max_w) + 1;

                if (soft_wrap && old_v_lines != new_v_lines)
                {
                    need_full_redraw = 1;
                }
            }
            else if (cur_y > 0)
            {
                int prev_len = rows[cur_y - 1].len;
                row_append_str(&rows[cur_y - 1], rows[cur_y].chars, rows[cur_y].len);
                row_delete(cur_y);
                cur_y--;
                cur_x = prev_len;
                need_full_redraw = 1;
            }
        }
        else if (c == '\r' || c == '\n')
        {
            ERow *row = &rows[cur_y];
            row_insert(cur_y + 1, &row->chars[cur_x], row->len - cur_x);
            row = &rows[cur_y];
            row->len = cur_x;
            row->chars[row->len] = '\0';
            cur_y++;
            cur_x = 0;
            need_full_redraw = 1;
        }
        else if (c >= 32 && c <= 126)
        {
            int max_w = screen_cols - 1;
            int old_v_lines = (rows[cur_y].len / max_w) + 1;

            row_insert_char(&rows[cur_y], cur_x, c);
            cur_x++;

            int new_v_lines = (rows[cur_y].len / max_w) + 1;

            if (soft_wrap && old_v_lines != new_v_lines)
            {
                need_full_redraw = 1;
            }
        }
        else if (c == '\033')
        {
            char seq[3];
            if (read(0, &seq[0], 1) <= 0)
            {
                continue;
            }
            if (read(0, &seq[1], 1) <= 0)
            {
                continue;
            }
            seq[2] = '\0';
            if (seq[0] == '[')
            {
                switch (seq[1])
                {
                case 'A': // up
                {
                    if (soft_wrap)
                    {
                        int max_w = screen_cols - 1;
                        int v_row = cur_x / max_w;
                        if (v_row > 0)
                        {
                            cur_x -= max_w;
                        }
                        else if (cur_y > 0)
                        {
                            int target_v_col = cur_x % max_w;
                            cur_y--;
                            int prev_v_rows = (rows[cur_y].len / max_w) + 1;
                            int new_x = (prev_v_rows - 1) * max_w + target_v_col;
                            if (new_x > rows[cur_y].len)
                            {
                                new_x = rows[cur_y].len;
                            }
                            cur_x = new_x;
                        }
                    }
                    else
                    {
                        if (cur_y > 0)
                        {
                            cur_y--;
                        }
                    }
                    break;
                }
                case 'B': // down
                {
                    if (soft_wrap)
                    {
                        int max_w = screen_cols - 1;
                        int v_row = cur_x / max_w;
                        int v_rows_curr = (rows[cur_y].len / max_w) + 1;
                        if (v_row < v_rows_curr - 1)
                        {
                            cur_x += max_w;
                            if (cur_x > rows[cur_y].len)
                            {
                                cur_x = rows[cur_y].len;
                            }
                        }
                        else if (cur_y < num_lines - 1)
                        {
                            int target_v_col = cur_x % max_w;
                            cur_y++;
                            cur_x = target_v_col;
                            if (cur_x > rows[cur_y].len)
                            {
                                cur_x = rows[cur_y].len;
                            }
                        }
                    }
                    else
                    {
                        if (cur_y < num_lines - 1)
                        {
                            cur_y++;
                        }
                    }
                    break;
                }
                case 'C': // right
                {
                    if (cur_x < rows[cur_y].len)
                    {
                        cur_x++;
                    }
                    else if (cur_y < num_lines - 1)
                    {
                        cur_y++;
                        cur_x = 0;
                    }
                    break;
                }
                case 'D': // left
                {
                    if (cur_x > 0)
                    {
                        cur_x--;
                    }
                    else if (cur_y > 0)
                    {
                        cur_y--;
                        cur_x = rows[cur_y].len;
                    }
                    break;
                }
                }

                if (cur_x > rows[cur_y].len)
                {
                    cur_x = rows[cur_y].len;
                }
            }
        }
    }

    for (int i = 0; i < num_lines; i++)
    {
        row_free(&rows[i]);
    }
    if (rows)
    {
        free(rows);
    }
    if (mega_buf)
    {
        free(mega_buf);
    }

    print("\033[?47l");
    exit(0);
}