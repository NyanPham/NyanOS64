#include "mq.h"
#include "mem/kmalloc.h"
#include "../string.h"

static MessageQueue_t *g_mq_root = NULL;

MessageQueue_t *mq_open(const char *name, int flags)
{
    MessageQueue_t *curr = g_mq_root;
    while (curr != NULL)
    {
        if (strcmp(name, curr->name) == 0)
        {
            return curr;
        }
        curr = curr->next;
    }

    MessageQueue_t *new_mq = (MessageQueue_t *)kmalloc(sizeof(MessageQueue_t));
    if (new_mq == NULL)
    {
        kprint("MQ_OPEN failed: OOM\n");
        return NULL;
    }

    strncpy(new_mq->name, name, 32);
    new_mq->name[31] = '\0';
    new_mq->max_msgs = 10;
    new_mq->msg_count = 0;
    new_mq->head = NULL;
    new_mq->tail = NULL;

    new_mq->next = g_mq_root;
    g_mq_root = new_mq;

    return new_mq;
}

int mq_send(MessageQueue_t *mq, const void *data, size_t size)
{
    while (mq->msg_count >= mq->max_msgs)
    {
        kprint("MQ_SEND: OOR\n");

        Task *curr_tsk = get_curr_task();
        curr_tsk->wait_next = mq->waiting_senders;
        mq->waiting_senders = curr_tsk;

        sched_block();
    }

    Message_t *msg = (Message_t *)kmalloc(sizeof(Message_t));
    if (msg == NULL)
    {
        kprint("MQ_SEND: OOM\n");
        return -1;
    }
    void *msg_data = kmalloc(size);
    if (msg_data == NULL)
    {
        kprint("MQ_SEND: OOM\n");
        kfree(msg);
        return -1;
    }

    memcpy(msg_data, data, size);

    msg->data = msg_data;
    msg->size = size;
    msg->next = NULL;

    if (mq->head == NULL)
    {
        mq->head = msg;
        mq->tail = msg;
    }
    else
    {
        mq->tail->next = msg;
        mq->tail = msg;
    }

    mq->msg_count += 1;

    if (mq->waiting_receivers != NULL)
    {
        Task *t = mq->waiting_receivers;
        mq->waiting_receivers = t->wait_next;

        t->state = TASK_READY;
        t->wait_next = NULL;
    }

    return 0;
}

int mq_receive(MessageQueue_t *mq, void *buf, size_t len)
{
    while (mq->head == NULL)
    {
        kprint("MQ_RECEIVE: Empty list\n");

        Task *curr_tsk = get_curr_task();
        curr_tsk->wait_next = mq->waiting_receivers;
        mq->waiting_receivers = curr_tsk;

        sched_block();
    }

    Message_t *msg = mq->head;
    mq->head = mq->head->next;
    msg->next = NULL;

    if (mq->head == NULL)
    {
        mq->tail = NULL;
    }

    mq->msg_count -= 1;

    int read_size = msg->size < len ? msg->size : len;
    memcpy(buf, msg->data, read_size);

    kfree(msg->data);
    kfree(msg);

    if (mq->waiting_senders != NULL)
    {
        Task *t = mq->waiting_senders;
        mq->waiting_senders = t->wait_next;

        t->state = TASK_READY;
        t->wait_next = NULL;
    }

    return read_size;
}

int mq_unlink(const char *name)
{
    MessageQueue_t *curr = g_mq_root;
    MessageQueue_t *prev = NULL;

    while (curr != NULL)
    {
        if (strcmp(name, curr->name) == 0)
        {
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    if (curr == NULL)
    {
        return -1;
    }

    if (prev == NULL)
    {
        g_mq_root = g_mq_root->next;
    }
    else
    {
        prev->next = curr->next;
        curr->next = NULL;
    }

    Message_t *curr_msg = curr->head;
    while (curr_msg != NULL)
    {
        Message_t *next = curr_msg->next;
        kfree(curr_msg->data);
        kfree(curr_msg);
        curr_msg = next;
    }

    kfree(curr);

    return 0;
}