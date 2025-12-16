#include "libc/libc.h"

int main(int argc, char** argv)
{
    print("Hello world from C language! (User Mode)\n", 0x00FF00);
    print("We are running on NyanOS\n", 0x00FFFF);
    
    if (argc > 1) 
    {
        print("Arguments received:\n", 0xFFFF00);
        for (int i = 0; i < argc; i++) 
        {
            print(" - ", 0xFFFFFF);
            print(argv[i], 0x00FFFF);
            print("\n", 0);
        }
    } 
    else 
    {
        print("No arguments provided.\n", 0xFF0000);
    }

    return 0;
}