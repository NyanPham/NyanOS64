#include "libc/libc.h"

int main(int argc, char** argv)
{
    print("Hello world from C language! (User Mode)\n");
    print("We are running on NyanOS\n");
    kprint("Userland calling kernel kprint() via syscall!\n");
    
    if (argc > 1) 
    {
        print("Arguments received:\n");
        for (int i = 0; i < argc; i++) 
        {
            print(" - ");
            print(argv[i]);
            print("\n");
        }
    } 
    else 
    {
        print("No arguments provided.\n");
    }

    print("Testing Malloc...\n");

    char* str = (char*)malloc(32);
    if (str == NULL)
    {
        print("Malloc failed!\n");
        return -1;
    }

    strcpy(str, "Hello heap!\n");
    print("String: ");
    print(str);

    int* nums = (int*)malloc(100 * sizeof(int));
    for (int i = 0; i < 5; i++)
    {
        nums[i] = i;
    }

    if (nums[3] == 3)
    {
        print("Numbers are OK!\n");
    }

    free(str);
    free(nums);

    print("Test Malloc Complete!\n");

    return 0;
}