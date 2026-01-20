#ifndef RING_BUF_H
#define RING_BUF_H

#include <stdint.h>

#define RING_BUF_SIZE 0x100 // 256

typedef struct RingBuf
{
    char buf[RING_BUF_SIZE];
    uint8_t head;
    uint8_t tail;
} RingBuf;

static inline void rb_init(RingBuf *rb)
{
    for (int i = 0; i < RING_BUF_SIZE; i++)
    {
        rb->buf[i] = 0;
    }

    rb->head = 0;
    rb->tail = 0;
}

static inline void rb_push(RingBuf *rb, char c)
{
    rb->buf[rb->head] = c;
    rb->head = (rb->head + 1) % RING_BUF_SIZE;
    if (rb->head == rb->tail)
    {
        rb->tail = (rb->tail + 1) % RING_BUF_SIZE;
    }
}

static inline int rb_pop(RingBuf *rb, char *c)
{
    if (rb->head == rb->tail)
    {
        return 0;
    }

    *c = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % RING_BUF_SIZE;
    return 1;
}

static inline int rb_is_full(RingBuf *rb)
{
    return ((rb->head + 1) % RING_BUF_SIZE) == rb->tail;
}

#endif