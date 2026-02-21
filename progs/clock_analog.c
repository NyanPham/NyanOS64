#include "libc/libc.h"
#include "event/event.h"

#include <stdint.h>
#include <stddef.h>

#define CLOCK_SIZE 200
#define CX (CLOCK_SIZE / 2)
#define CY (CLOCK_SIZE / 2)

static const float sin_60[60] = {
    0.000, 0.104, 0.207, 0.309, 0.406, 0.500, 0.587, 0.669, 0.743, 0.809,
    0.866, 0.913, 0.951, 0.978, 0.994, 1.000, 0.994, 0.978, 0.951, 0.913,
    0.866, 0.809, 0.743, 0.669, 0.587, 0.500, 0.406, 0.309, 0.207, 0.104,
    0.000, -0.104, -0.207, -0.309, -0.406, -0.500, -0.587, -0.669, -0.743, -0.809,
    -0.866, -0.913, -0.951, -0.978, -0.994, -1.000, -0.994, -0.978, -0.951, -0.913,
    -0.866, -0.809, -0.743, -0.669, -0.587, -0.500, -0.406, -0.309, -0.207, -0.104};

static const float mcos_60[60] = {
    -1.000, -0.994, -0.978, -0.951, -0.913, -0.866, -0.809, -0.743, -0.669, -0.587,
    -0.500, -0.406, -0.309, -0.207, -0.104, -0.000, 0.104, 0.207, 0.309, 0.406,
    0.500, 0.587, 0.669, 0.743, 0.809, 0.866, 0.913, 0.951, 0.978, 0.994,
    1.000, 0.994, 0.978, 0.951, 0.913, 0.866, 0.809, 0.743, 0.669, 0.587,
    0.500, 0.406, 0.309, 0.207, 0.104, 0.000, -0.104, -0.207, -0.309, -0.406,
    -0.500, -0.587, -0.669, -0.743, -0.809, -0.866, -0.913, -0.951, -0.978, -0.994};

static uint32_t frame_buf[CLOCK_SIZE * CLOCK_SIZE];

void draw_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1)
    {
        if (x0 >= 0 && x0 < CLOCK_SIZE && y0 >= 0 && y0 < CLOCK_SIZE)
        {
            frame_buf[y0 * CLOCK_SIZE + x0] = color;
        }
        if (x0 == x1 && y0 == y1)
        {
            break;
        }

        int e2 = 2 * err;
        if (e2 > -dy)
        {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void clear_bg()
{
    for (int i = 0; i < CLOCK_SIZE * CLOCK_SIZE; i++)
    {
        frame_buf[i] = 0x000000;
    }
}

void draw_face()
{
    for (int i = 0; i < 60; i++)
    {
        int r1 = (i % 5 == 0) ? (CLOCK_SIZE / 2 - 15) : (CLOCK_SIZE / 2 - 5);
        int r2 = CLOCK_SIZE / 2 - 2;
        int x1 = CX + (int)(sin_60[i] * r1);
        int y1 = CY + (int)(mcos_60[i] * r1);
        int x2 = CX + (int)(sin_60[i] * r2);
        int y2 = CY + (int)(mcos_60[i] * r2);

        draw_line(x1, y1, x2, y2, 0xFFFFFF);
    }
}

int main(int argc, char **argv)
{
    WinParams_t wp;
    wp.x = 200;
    wp.y = 150;
    wp.width = CLOCK_SIZE;
    wp.height = CLOCK_SIZE;
    strcpy(wp.title, "Analog Clock");
    wp.flags = WIN_MOVABLE | WIN_MINIMIZABLE | WIN_RESIZABLE;

    int win_fd = win_create(&wp);
    if (win_fd < 0)
    {
        return -1;
    }

    int h = 10, m = 10, s = 0;

    Event e;
    Time_t t;
    int prev_sec = -1;

    while (1)
    {
        if (get_event(&e, O_NONBLOCK) > 0)
        {
            // log here
        }

        sys_get_time(&t);

        if (t.secs != prev_sec)
        {
            clear_bg();
            draw_face();

            int s_idx = t.secs % 60;
            int m_idx = t.mins % 60;
            int h_idx = (((t.hrs + 7) % 12) * 5 + (t.mins / 12)) % 60;

            // draw hours
            draw_line(CX, CY, CX + (int)(sin_60[h_idx] * 40), CY + (int)(mcos_60[h_idx] * 40), 0xFF0000);

            // draw minutes
            draw_line(CX, CY, CX + (int)(sin_60[m_idx] * 65), CY + (int)(mcos_60[m_idx] * 65), 0x00FF00);

            // draw seconds
            draw_line(CX, CY, CX + (int)(sin_60[s_idx] * 80), CY + (int)(mcos_60[s_idx] * 80), 0x0000FF);

            blit(0, 0, CLOCK_SIZE, CLOCK_SIZE, frame_buf);

            prev_sec = t.secs;
        }

        sleep(136);
    }

    return 0;
}