#include "ansi.h"


uint32_t g_ansi_palette[]=
    {
        Black,   // 30
        Red,     // 31
        Green,   // 32
        Yellow,  // 33
        Blue,    // 34
        Magenta, // 35
        Cyan,    // 36
        White,   // 37
};

int ansi_atoi(const char *s)
{
    int res = 0;

    while (*s >= '0' && *s <= '9')
    {
        res = res * 10 + (*s - '0');
        s++;
    }

    return res;
}

static void parse_ansi_params(char* buf, int* out_params, int* out_cnt)
{
    *out_cnt = 0;
    for (uint8_t i = 0; i < 4; i++)
    {
        out_params[i] = 0;
    }

    char *ptr = buf;
    char* start = ptr;

    while (*ptr)
    {
        if (*ptr == ';')
        {
            *ptr = 0;
            if (*out_cnt < 4)
            {
                out_params[(*out_cnt)++] = ansi_atoi(start);
            }
            start = ptr + 1;
        }
        ptr++;
    }
    if (*out_cnt < 4)
    {
        out_params[(*out_cnt)++] = ansi_atoi(start);
    }
}

void ansi_write_char(AnsiContext *ctx, char c, const AnsiDriver *driver, void *driver_data)
{
    // Round 1: I'm Nor-malle
    if (ctx->state == ANSI_NORMAL)
    {
        if (c == '\033')
        {
            ctx->state = ANSI_ESC;
        }
        else
        {
            if (driver->put_char)
            {
                driver->put_char(driver_data, c);
            }
        }
    }
    // Round 2: Let's run away from this Escape room
    else if (ctx->state == ANSI_ESC)
    {
        if (c == '[')
        {
            ctx->state = ANSI_CSI;
            ctx->idx = 0;
            for (uint8_t i = 0; i < ANSI_BUF_SIZE; i++)
            {
                ctx->buf[i] = 0;
            }
        }
        else 
        {
            // unknown ESC command ? return to normal
            ctx->state = ANSI_NORMAL;
        }
    }
    // Round 3: Just do it
    else if (ctx->state == ANSI_CSI)
    {
        if ((c >= '0' && c <= '9') || c == ';')
        {
            if (ctx->idx < ANSI_BUF_SIZE - 1)
            {
                ctx->buf[ctx->idx++] = c;
            }
        }
        else 
        {
            // exec the commands
            int params[4] = {0};
            int count = 0;
            parse_ansi_params(ctx->buf, params, &count);

            switch (c)
            {
            case 'H': // move cursor
            case 'f':
            {
                int r = (params[0] > 0) ? params[0] : 1;
                int c = (params[1] > 0) ? params[1] : 1;
                if (driver->set_cursor)
                {
                    driver->set_cursor(driver_data, c - 1, r - 1);
                }
                break;
            }
            case 'J': // clear screen
            {
                if (params[0] == 2)
                {
                    if (driver->clear_screen)
                    {
                        driver->clear_screen(driver_data, 2);
                    }
                }
                break;
            }
            case 'm': // set color
            {
                for (uint8_t i = 0; i < count; i++)
                {
                    int code = params[i];
                    if (code >= 30 && code <= 37)
                    {
                        uint32_t color = g_ansi_palette[code - 30];
                        ctx->color = color;
                        if (driver->set_color)
                        {
                            driver->set_color(driver_data, color);
                        }
                    }
                    else if (code == 0)
                    {
                        ctx->color = White;
                        if (driver->set_color)
                        {
                            driver->set_color(driver_data, White);
                        }
                    }
                }
                break;
            }
            }
            ctx->state = ANSI_NORMAL;
        }
    }
}