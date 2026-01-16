#include "libc/libc.h"

int main()
{
    kprint("Hello from Parent (PID: ");
    kprint_int(getpid());
    kprint(")\n");

    int pid = fork();

    if (pid == 0)
    {
        kprint("I am Child! My PID is: ");
        kprint_int(pid);
        kprint("\nExiting child...\n");
        exit(123);
    }
    else
    {
        kprint("I am Parent! I created child with PID: ");
        kprint_int(pid);
        kprint("\nWaiting for child...\n");

        int status;
        int child_pid = waitpid(pid, &status);

        kprint("Child finished!\n");
        kprint("Child PID: ");
        kprint_int(child_pid);
        kprint("\nChild Status: ");
        kprint_int(status);
        kprint("\n");
    }
    return 0;
}