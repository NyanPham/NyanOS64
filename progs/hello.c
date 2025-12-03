#include "libc/libc.h"

int main()
{
    print("Hello world from C language! (User Mode)\n", 0x00FF00);
    print("We are running on NyanOS\n", 0x00FFFF);
    return 0;
}