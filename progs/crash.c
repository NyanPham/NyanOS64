#include "libc/libc.h"

int main()
{
    print("I am going to crash now...\n");
    
    int *ptr = (int *)0xDEADBEEF;
    *ptr = 123;

    print("This line should never run!\n");
    return 0;
}