#include "libc/libc.h"

void main()
{
    print("MQ Writer: Connecting to queue '/demo_mq'...\n");

    mqd_t mq = mq_open("/demo_mq", O_CREAT);

    if (mq == NULL)
    {
        print("MQ Writer: Failed to open MQ. OOM?\n");
        exit(1);
    }
    print("MQ Writer: Open OK.\n");

    char *msgs[] = {
        "Hello from NyanOS!",
        "Message Queue is working!",
        "IPC is fun :D",
        "Bye bye!"};

    for (int i = 0; i < 4; i++)
    {
        print("MQ Writer: Sending '");
        print(msgs[i]);
        print("'...\n");

        int res = mq_send(mq, msgs[i], strlen(msgs[i]) + 1);
        if (res < 0)
        {
            print("MQ Writer: Send failed (Queue full?)\n");
        }
        else
        {
            print("MQ Writer: Sent OK.\n");
        }

        sleep(10000000);
    }

    print("MQ Writer: Done.\n");
    exit(0);
}