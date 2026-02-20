#ifndef EVENT_H
#define EVENT_H

#include <stdint.h>

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