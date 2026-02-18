#include "libc/libc.h"
#include "libc/float_print.h"

int main()
{
    float val = 1.5f;
    float step = 0.001f;
    int pid = getpid();
    char buf[64];
    char pid_str[16];

    int_to_str(pid, pid_str, 0);

    print("FPU Test (PID ");
    print(pid_str);
    print(") Started.\n");

    int counter = 0;

    while (1)
    {
        val += step;
        float check = val * 2.0f;

        if (counter % 5000 == 0)
        {
            print("[PID ");
            print(pid_str);
            print("] Val: ");

            ftoa(val, buf, 4);
            print(buf);

            print(" | *2: ");
            ftoa(check, buf, 4);
            print(buf);

            print("\n");
        }

        counter++;
        sleep(1000);
    }
    return 0;
}