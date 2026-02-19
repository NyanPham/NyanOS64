#include "libc/libc.h"

#define COLOR_BG 0xFF111111
#define COLOR_LED 0xFF00FF00
#define COLOR_OFF 0xFF222222

#define SEG_THICK 4
#define SEG_LEN 15
#define TITLE_BAR_H 20

/*
      A
     ---
  F |   | B
     -G-
  E |   | C
     ---
      D
*/

void draw_digit(int ox, int oy, int num)
{
    // order: A, B, C, D, E, F, G
    uint8_t code[10][7] = {
        {1, 1, 1, 1, 1, 1, 0}, // 0
        {0, 1, 1, 0, 0, 0, 0}, // 1
        {1, 1, 0, 1, 1, 0, 1}, // 2
        {1, 1, 1, 1, 0, 0, 1}, // 3
        {0, 1, 1, 0, 0, 1, 1}, // 4
        {1, 0, 1, 1, 0, 1, 1}, // 5
        {1, 0, 1, 1, 1, 1, 1}, // 6
        {1, 1, 1, 0, 0, 0, 0}, // 7
        {1, 1, 1, 1, 1, 1, 1}, // 8
        {1, 1, 1, 1, 0, 1, 1}  // 9
    };

    draw_rect(ox + SEG_THICK, oy, SEG_LEN, SEG_THICK, code[num][0] ? COLOR_LED : COLOR_OFF);
    draw_rect(ox + SEG_LEN + SEG_THICK, oy + SEG_THICK, SEG_THICK, SEG_LEN, code[num][1] ? COLOR_LED : COLOR_OFF);
    draw_rect(ox + SEG_LEN + SEG_THICK, oy + SEG_THICK * 2 + SEG_LEN, SEG_THICK, SEG_LEN, code[num][2] ? COLOR_LED : COLOR_OFF);
    draw_rect(ox + SEG_THICK, oy + SEG_THICK * 2 + SEG_LEN * 2, SEG_LEN, SEG_THICK, code[num][3] ? COLOR_LED : COLOR_OFF);
    draw_rect(ox, oy + SEG_THICK * 2 + SEG_LEN, SEG_THICK, SEG_LEN, code[num][4] ? COLOR_LED : COLOR_OFF);
    draw_rect(ox, oy + SEG_THICK, SEG_THICK, SEG_LEN, code[num][5] ? COLOR_LED : COLOR_OFF);
    draw_rect(ox + SEG_THICK, oy + SEG_THICK + SEG_LEN, SEG_LEN, SEG_THICK, code[num][6] ? COLOR_LED : COLOR_OFF);
}

void draw_colon(int ox, int oy, int tick)
{
    uint32_t c = (tick % 2 == 0) ? COLOR_LED : COLOR_OFF;
    draw_rect(ox + 2, oy + SEG_LEN / 2 + 5, 4, 4, c);
    draw_rect(ox + 2, oy + SEG_LEN * 1.5 + 5, 4, 4, c);
}

void main(int argc, char **argv)
{
    WinParams_t params;
    params.x = 700;
    params.y = 400;
    params.width = 240;
    params.height = 80 + TITLE_BAR_H;
    params.flags = WIN_MOVABLE;
    strcpy(params.title, "Digital Clock");

    if (win_create(&params) < 0)
        exit(1);

    Time_t t;
    int prev_sec = -1;

    draw_rect(0, TITLE_BAR_H, params.width, params.height - TITLE_BAR_H, COLOR_BG);

    while (1)
    {
        sys_get_time(&t);

        if (t.secs != prev_sec)
        {
            uint8_t h = (t.hrs + 7) % 24;
            uint8_t m = t.mins;
            uint8_t s = t.secs;

            int start_x = 10;
            int start_y = 20 + TITLE_BAR_H;
            int spacing = 30;

            draw_rect(0, TITLE_BAR_H, params.width, params.height - TITLE_BAR_H, COLOR_BG);

            // hrs
            draw_digit(start_x, start_y, h / 10);
            draw_digit(start_x + spacing, start_y, h % 10);

            // :
            draw_colon(start_x + spacing * 2, start_y, s);

            // mins
            draw_digit(start_x + spacing * 2 + 15, start_y, m / 10);
            draw_digit(start_x + spacing * 3 + 15, start_y, m % 10);

            // :
            draw_colon(start_x + spacing * 4 + 15, start_y, s);

            // secs
            draw_digit(start_x + spacing * 4 + 30, start_y, s / 10);
            draw_digit(start_x + spacing * 5 + 30, start_y, s % 10);

            prev_sec = s;
        }

        sleep(1000);
    }

    exit(0);
}