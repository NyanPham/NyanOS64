#ifndef ANSI_H
#define ANSI_H

#include "kern_defs.h"
#include <stdint.h>

typedef enum
{
    ANSI_NORMAL,
    ANSI_ESC,
    ANSI_CSI
} ansi_state_t;

#define ANSI_BUF_SIZE 32

extern uint32_t g_ansi_palette[];

typedef struct
{
    uint32_t color;
    ansi_state_t state;
    char buf[ANSI_BUF_SIZE];
    int idx;
} AnsiContext;

// Interface: drivers and gui may call this
typedef struct
{
    void (*put_char)(void *data, char c);
    void (*set_color)(void *data, uint32_t color);
    void (*set_cursor)(void *data, int x, int y);
    void (*clear_screen)(void *data, int mode);
    void (*scroll)(void *data);
} AnsiDriver;

int ansi_atoi(const char *s);
void ansi_write_char(AnsiContext *ctx, char c, const AnsiDriver *driver, void *driver_data);

#endif