#ifndef MQ_H
#define MQ_H

#include "sched/sched.h"
#include <stdint.h>

typedef struct Message
{
    void *data;
    size_t size;
    struct Message *next;
} Message_t;

typedef struct MessageQueue
{
    char name[32];
    Message_t *head;
    Message_t *tail;
    int msg_count;
    int max_msgs;
    struct MessageQueue *next;
    Task *waiting_receivers;
    Task *waiting_senders;
} MessageQueue_t;

MessageQueue_t *mq_open(const char *name, int flags);
int mq_send(MessageQueue_t *mq, const void *data, size_t size);
int mq_receive(MessageQueue_t *mq, void *buf, size_t len);
int mq_unlink(const char *name);

#endif