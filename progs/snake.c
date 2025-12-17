#include "libc/libc.h"

int main()
{
    print("\033[2J");

    // draw walls
    print("\033[34m"); // Blue
    print("\033[1;1H####################");
    print("\033[2;1H#                  #");
    print("\033[3;1H#                  #");
    print("\033[4;1H####################");

    // draw snakes
    print("\033[32m"); // Green
    print("\033[2;5H@--o");

    // draw food
    print("\033[31m"); // Red
    print("\033[3;15H*");

    // reset colors and push the cursor down
    print("\033[0m"); // Reset
    print("\033[6;1HTest Complete!\n");

    return 0;
}