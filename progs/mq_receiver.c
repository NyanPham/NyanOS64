#include "libc/libc.h"

void main()
{
    print("MQ Reader: Connecting to queue '/demo_mq'...\n");

    mqd_t mq = mq_open("/demo_mq", 0);

    if (mq == NULL)
    {
        print("MQ Reader: Queue not found. Run writer first?\n");
        exit(1);
    }
    print("MQ Reader: Connected! Waiting for messages...\n");

    char buf[64];

    while (1)
    {
        memset(buf, 0, 64);
        int bytes = mq_receive(mq, buf, 64);

        if (bytes > 0)
        {
            print("Received: [");
            print(buf);
            print("]\n");

            if (strcmp(buf, "Bye bye!") == 0)
                break;
        }
    }

    print("\nMQ Reader: Finished. Cleaning up...\n");
    mq_unlink("/demo_mq");
    exit(0);
}