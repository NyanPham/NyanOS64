#include "libc/libc.h"

int main(void)
{
    WinParams_t params;
    params.x = 200;
    params.y = 150;
    params.width = 300;
    params.height = 200;
    params.flags = WIN_MOVABLE | WIN_RESIZABLE | WIN_MINIMIZABLE;
    strcpy(params.title, "Event & Mouse Paint Tester");

    if (win_create(&params) < 0)
    {
        print("[TEST EVENT QUEUE] Failed to create GUI window!\n");
        exit(1);
    }

    uint32_t box_color = 0xFF00FFCC;
    draw_rect(0, 0, params.width, params.height, 0xFF222222);

    Event e;
    while (1)
    {
        int res = get_event(&e, 0);
        if (res == 1)
        {
            if (e.type == EVENT_KEY_PRESSED)
            {
                if (e.key == 'q')
                {
                    break;
                }
                else if (e.key == 'c')
                {
                    box_color = 0xFF000000 | ((rand() % 200 + 55) << 16) | ((rand() % 200 + 55) << 8) | (rand() % 200 + 55);
                    draw_rect(10, 10, 24, 24, box_color);
                }
            }
            else if (e.type == MOUSE_CLICK)
            {
                if (e.mouse.buttons & 0x02)
                {
                    // Right-click clears canvas
                    draw_rect(0, 0, 2000, 2000, 0xFF222222);
                }
                else
                {
                    // Left-click paints a 16x16 square centered on cursor
                    draw_rect(e.mouse.x - 8, e.mouse.y - 8, 16, 16, box_color);
                }
            }
            else if (e.type == EVENT_WIN_RESIZE)
            {
                draw_rect(0, 0, 2000, 2000, 0xFF222222);
                draw_rect(10, 10, 24, 24, box_color);
            }
        }
    }

    exit(0);
    return 0;
}