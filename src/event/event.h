#ifndef EVENT_H
#define EVENT_H

#include <stdint.h>

#define EVENT_QUEUE_SIZE 0x100 // 256

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

typedef struct EventBuf
{
    Event queue[EVENT_QUEUE_SIZE];
    uint8_t head;
    uint8_t tail;
} EventBuf;

static inline void event_queue_init(EventBuf *event_queue)
{
    for (int i = 0; i < EVENT_QUEUE_SIZE; i++)
    {
        event_queue->queue[i].type = EMPTY;
        event_queue->queue[i].modifiers = 0;
    }

    event_queue->head = 0;
    event_queue->tail = 0;
}

static inline void event_queue_push(EventBuf *event_queue, Event e)
{
    uint64_t *dest = (uint64_t *)&event_queue->queue[event_queue->head];
    uint64_t *src = (uint64_t *)&e;
    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = src[2];

    event_queue->head = (event_queue->head + 1) % EVENT_QUEUE_SIZE;
    if (event_queue->head == event_queue->tail)
    {
        event_queue->tail = (event_queue->tail + 1) % EVENT_QUEUE_SIZE;
    }
}

static inline int event_queue_pop(EventBuf *event_queue, Event *e)
{
    if (event_queue->head == event_queue->tail)
    {
        return 0;
    }

    uint64_t *dest = (uint64_t *)e;
    uint64_t *src = (uint64_t *)&event_queue->queue[event_queue->tail];
    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = src[2];

    event_queue->tail = (event_queue->tail + 1) % EVENT_QUEUE_SIZE;
    return 1;
}

extern EventBuf g_event_queue;
void event_init(void);

#endif