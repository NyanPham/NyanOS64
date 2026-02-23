#include "libc/libc.h"

#define MAX_LINES 136
#define MAX_COLS 96

char text_buf[MAX_LINES][MAX_COLS];
int num_lines = 1;
int cur_x = 0;
int cur_y = 0;
char filepath[128];

void edtr_cls()
{
    print("\033[2J\033[1;1H");
}

void edtr_drw()
{
    edtr_cls();

    for (int i = 0; i < num_lines; i++)
    {
        move_cursor(i + 1, 1);
        print(text_buf[i]);
    }

    move_cursor(20, 1);
    print("\033[7m [Ctrl+S] Save | [Ctrl+Q] Quit | NyanEdit \033[0m");

    move_cursor(cur_y + 1, cur_x + 1);
}

void edtr_ld()
{
    int fd = open(filepath, O_RDONLY);
    if (fd < 0)
    {
        return;
    }

    char buf[512];
    int n;
    num_lines = 1;
    int col = 0;

    while ((n = read(fd, buf, 512)) > 0)
    {
        for (int i = 0; i < n; i++)
        {
            if (buf[i] == '\n')
            {
                text_buf[num_lines - 1][col] = '\0';
                num_lines++;
                col = 0;
            }
            else if (col < MAX_COLS - 1)
            {
                text_buf[num_lines - 1][col++] = buf[i];
            }
        }
    }
    text_buf[num_lines - 1][col] = '\0';
    close(fd);
}

void edtr_sv()
{
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
    {
        return;
    }

    for (int i = 0; i < num_lines; i++)
    {
        write(fd, text_buf[i], strlen(text_buf[i]));
        if (i < num_lines - 1)
        {
            write(fd, "\n", 1);
        }
    }
    close(fd);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        print("Usage: nyanedit <filename>\n");
        exit(1);
    }

    strcpy(filepath, argv[1]);

    memset(text_buf, 0, MAX_LINES * MAX_COLS);
    edtr_ld();

    while (1)
    {
        edtr_drw();

        char c;
        if (read(0, &c, 1) <= 0)
        {
            continue;
        }

        if (c == '\x11') // ctrl + q (to quit)
        {
            break;
        }
        else if (c == '\x13') // ctrl + s (to save)
        {
            edtr_sv();
        }
        else if (c == '\b' || c == 0x7F) // backspace
        {
            if (cur_x > 0)
            {
                int len = strlen(text_buf[cur_y]);
                for (int i = cur_x; i <= len; i++)
                {
                    text_buf[cur_y][i - 1] = text_buf[cur_y][i];
                }
                cur_x--;
            }
            else if (cur_y > 0)
            {
                int prev_len = strlen(text_buf[cur_y - 1]);
                int curr_len = strlen(text_buf[cur_y]);

                if (prev_len + curr_len < MAX_COLS - 1)
                {
                    strcpy(&text_buf[cur_y - 1][prev_len], text_buf[cur_y]);

                    for (int i = cur_y; i < num_lines - 1; i++)
                    {
                        strcpy(text_buf[i], text_buf[i + 1]);
                    }

                    memset(text_buf[num_lines - 1], 0, MAX_COLS);
                    num_lines--;
                    cur_y--;
                    cur_x = prev_len;
                }
            }
        }
        else if (c == '\r' || c == '\n') // enter
        {
            if (num_lines < MAX_LINES)
            {
                num_lines++;
                cur_y++;
                cur_x = 0;
            }
        }
        else if (c >= 32 && c <= 126) // [a-z][A-Z][0-9]
        {
            int len = strlen(text_buf[cur_y]);
            if (len < MAX_COLS - 1)
            {
                for (int i = len; i >= cur_x; i--)
                {
                    text_buf[cur_y][i + 1] = text_buf[cur_y][i];
                }

                text_buf[cur_y][cur_x] = c;
                cur_x++;
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
                    if (cur_y > 0)
                    {
                        cur_y--;
                    }
                    break;
                }
                case 'B': // down
                {
                    if (cur_y < num_lines - 1)
                    {
                        cur_y++;
                    }
                    break;
                }
                case 'C': // right
                {
                    if (cur_x < strlen(text_buf[cur_y]))
                    {
                        cur_x++;
                    }
                    break;
                }
                case 'D': // left
                {
                    if (cur_x > 0)
                    {
                        cur_x--;
                    }
                    break;
                }
                }

                int row_len = strlen(text_buf[cur_y]);
                if (cur_x > row_len)
                {
                    cur_x = row_len;
                }
            }
            continue;
        }
    }

    edtr_cls();
    exit(0);
}