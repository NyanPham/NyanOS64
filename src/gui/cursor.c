#include "cursor.h"
#include "drivers/mouse.h"
#include "drivers/video.h"
#include "kern_defs.h"

void draw_mouse()
{
    int64_t mx = mouse_get_x();
    int64_t my = mouse_get_y();

    for (int y = 0; y < 4; y++)
    {
        for (int x = 0; x < 4; x++)
        {
            video_plot_pixel(mx + x, my + y, Red);
        }
    }
}