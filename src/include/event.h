#ifndef EVENT_H
#define EVENT_H

#include <stdint.h>

#define MOD_CTRL (1 << 0)  // 0001
#define MOD_ALT (1 << 1)   // 0010
#define MOD_SHIFT (1 << 2) // 0100

typedef enum
{
    EMPTY,
    EVENT_KEY_PRESSED,
    MOUSE_CLICK,
    EVENT_WIN_RESIZE,
} EventType;

typedef struct Event
{
    EventType type;
    uint8_t modifiers;

    union
    {
        char key;
        struct
        {
            int x, y;
            uint8_t buttons;
        } mouse;
        struct
        {
            int64_t win_owner_pid;
        } resize_event;
    };
} Event;

#endif