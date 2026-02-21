#include "libc/libc.h"

int main()
{
    WinParams_t params;
    params.x = 200;
    params.y = 150;
    params.width = 300;
    params.height = 200;
    params.flags = WIN_MOVABLE | WIN_RESIZABLE;
    strcpy(params.title, "Event Queue Tester");

    if (win_create(&params) < 0)
    {
        print("[TEST EVENT QUEUE] Failed to create GUI window!\n");
        exit(1);
    }

    uint32_t box_color = 0xFFFF0000;

    draw_rect(0, 0, params.width, params.height, 0xFF333333);

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
                    box_color = 0xFF000000 | ((rand() % 255) << 16) | ((rand() % 255) << 8) | (rand() % 255);
                    draw_rect(100, 50, 100, 100, box_color);
                }
            }
            else if (e.type == EVENT_WIN_RESIZE)
            {
                draw_rect(0, 0, 2000, 2000, 0xFF333333);
                draw_rect(100, 50, 100, 100, box_color);
            }
        }
    }

    exit(0);
    return 0;
}