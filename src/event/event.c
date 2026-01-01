#include "event.h"

EventBuf g_event_queue;

void event_init(void)
{
    event_queue_init(&g_event_queue);
}