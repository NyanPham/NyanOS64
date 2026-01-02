#include "libc/libc.h"

// Game Settings
#define WIDTH 40
#define HEIGHT 20
#define MAX_LEN 1000
#define DELAY 5000000

// Snake body (circular buffer)
int *snake_x = NULL;
int *snake_y = NULL;

int head = 0;
int tail = 0;
int xdir = 1;
int ydir = 0;

int food_x;
int food_y;
int score = 0;
int running = 1;

void print_num(int num)
{
    char buf[16];
    int i = 0;
    if (num == 0)
    {
        print("0");
        return;
    }

    char tmp[10];
    int j = 0;

    while (num > 0)
    {
        tmp[j++] = (num % 10) + '0';
        num /= 10;
    }

    while (j > 0)
    {
        buf[i++] = tmp[--j];
    }

    buf[i] = 0;
    print(buf);
}

void spawn_food(void)
{
    food_x = (rand() % (WIDTH - 2)) + 2;
    food_y = (rand() % (HEIGHT - 2)) + 2;

    move_cursor(food_y, food_x);
    print("\033[31m*\033[0m");
}

void game_over(const char *reason)
{
    move_cursor(HEIGHT + 2, 1);
    print("\033[31m");
    print("GAME OVER (");
    print(reason);
    print(")\nFinal Score: ");
    print_num(score);
    print("\nPress 'q' to return to Shell...\033[0m");

    while (1)
    {
        int k = get_key();
        if (k == 'q')
            break;
    }
}

int main(void)
{
    // Create the game terminal
    if (create_term(100, 100, WIDTH * 8 + 16, HEIGHT * 8 + 40, "Snyake") < 0)
    {
        return -1;
    }

    // Allocate memory for snake
    snake_x = (int *)malloc(MAX_LEN * sizeof(int));
    snake_y = (int *)malloc(MAX_LEN * sizeof(int));

    if (snake_x == NULL || snake_y == NULL)
    {
        print("MALLOC FAILED! Not enough memory for Snake.\n");
        return -1;
    }

    // Setup screen
    print("\033[2J");
    print("\033[?25l");

    print("\033[34m");

    char wall_chunk[WIDTH + 1];
    memset(wall_chunk, '#', WIDTH);
    wall_chunk[WIDTH] = 0;

    move_cursor(1, 1);
    print(wall_chunk);
    move_cursor(HEIGHT, 1);
    print(wall_chunk);

    for (int i = 2; i < HEIGHT; i++)
    {
        move_cursor(i, 1);
        print("#");
        move_cursor(i, WIDTH);
        print("#");
    }
    print("\033[0m");

    // Init snake
    head = 0;
    tail = 0;
    snake_x[0] = 10;
    snake_y[0] = 10;
    move_cursor(snake_y[0], snake_x[0]);
    print("\033[32m@\033[0m");

    srand(12345);
    spawn_food();

    while (running)
    {
        move_cursor(HEIGHT + 1, 1);
        print("Score: ");
        print_num(score);
        print(" (WASD to move)   ");

        // Handle input
        while (1)
        {
            int key = get_key();
            if (key == 0)
                break;
            if (key == 'q')
                running = 0;
            if (key == 'w' && ydir != 1)
            {
                xdir = 0;
                ydir = -1;
            }
            if (key == 's' && ydir != -1)
            {
                xdir = 0;
                ydir = 1;
            }
            if (key == 'a' && xdir != 1)
            {
                xdir = -1;
                ydir = 0;
            }
            if (key == 'd' && xdir != -1)
            {
                xdir = 1;
                ydir = 0;
            }
        }

        if (!running)
            break;

        int new_x = snake_x[head] + xdir;
        int new_y = snake_y[head] + ydir;

        if (new_x <= 1 || new_x >= WIDTH || new_y <= 1 || new_y >= HEIGHT)
        {
            game_over("Hit Wall");
            break;
        }

        int curr = tail;
        int collision = 0;
        while (curr != head)
        {
            if (snake_x[curr] == new_x && snake_y[curr] == new_y)
            {
                collision = 1;
                break;
            }
            curr = (curr + 1) % MAX_LEN;
        }
        if (collision)
        {
            game_over("Bit Self");
            break;
        }

        head = (head + 1) % MAX_LEN;
        snake_x[head] = new_x;
        snake_y[head] = new_y;

        move_cursor(new_y, new_x);
        print("\033[32m@\033[0m");

        int prev_head = (head - 1 + MAX_LEN) % MAX_LEN;
        if (prev_head != tail)
        {
            move_cursor(snake_y[prev_head], snake_x[prev_head]);
            print("\033[32mo\033[0m");
        }

        if (new_x == food_x && new_y == food_y)
        {
            score++;
            spawn_food();
        }
        else
        {
            move_cursor(snake_y[tail], snake_x[tail]);
            print(" ");
            tail = (tail + 1) % MAX_LEN;
        }

        sleep(DELAY);
    }

    print("\033[0m");
    print("\033[2J");
    move_cursor(1, 1);
    print("\033[?25h");

    free(snake_x);
    free(snake_y);

    return 0;
}