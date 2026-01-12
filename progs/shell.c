#include "libc/libc.h"
#include "syscall_args.h"

#define HIST_MAX 10
#define CMD_MAX_LEN 128

char history[HIST_MAX][CMD_MAX_LEN];
int hist_count = 0;
int hist_curr = 0;

void clear_curr_line(int len)
{
    for (int k = 0; k < len; k++)
    {
        print("\b \b");
    }
}

int parse_cmd(char *line, char **argv)
{
    int argc = 0;
    int in_tok = 0;

    while (*line)
    {
        // a line ends with whitespace
        if (*line == ' ' || *line == '\n' || *line == '\t')
        {
            *line = 0;
            in_tok = 0;
        }
        else if (in_tok == 0)
        {
            argv[argc++] = line;
            in_tok = 1;
        }
        line++;
    }
    argv[argc] = NULL;
    return argc;
}

int main()
{
    char line[128];
    char cwd[128];
    char *argv[16];

    // start the shell
    print("\033[2J\033[1;1H");
    print("Welcome to NyanOS Shell!\n");
    print("Type 'help' to list available commands: hi, reboot, exit, clear, ls, pwd, cd, cat, help, <program>.\n\n");

    while (1)
    {
        // show the prompt: "NyanOS /curr/path>"
        if (getcwd(cwd, 128) != NULL)
        {
            print("\033[36mNyanOS "); // cyan
            print(cwd);
            print("> \033[0m"); // reset color
        }
        else
        {
            print("NyanOS \?\?\?>");
        }

        // use blocking i/o to get the input
        int i = 0;
        hist_curr = hist_count;
        while (1)
        {
            char c;
            int n = read(0, &c, 1);

            if (n > 0)
            {
                if (c == '\n')
                {
                    print("\n");

                    // save to history
                    line[i] = 0;
                    if (i > 0)
                    {
                        int nxt_slot = hist_count % HIST_MAX;
                        strcpy(history[nxt_slot], line);
                        hist_count++;
                    }
                    break;
                }

                if (c == '\b')
                {
                    if (i > 0)
                    {
                        i--;
                        print("\b");
                    }
                    continue;
                }

                if (c == '\033')
                {
                    char seq[2];
                    if (read(0, &seq[0], 1) > 0 && read(0, &seq[1], 1) > 0)
                    {
                        if (seq[0] == '[')
                        {
                            if (seq[1] == 'A') // is KEY_UP
                            {
                                if (hist_count > 0)
                                {
                                    int min_idx = (hist_count >= HIST_MAX) ? (hist_count - HIST_MAX) : 0;
                                    if (hist_curr > min_idx)
                                    {
                                        hist_curr--;
                                        clear_curr_line(i);
                                        int real_idx = hist_curr % HIST_MAX;
                                        strcpy(line, history[real_idx]);
                                        i = strlen(line);
                                        print(line);
                                    }
                                }
                            }
                            else if (seq[1] == 'B') // is KEY_DOWN
                            {
                                if (hist_curr < hist_count)
                                {
                                    hist_curr++;
                                    clear_curr_line(i);

                                    if (hist_curr == hist_count)
                                    {
                                        i = 0;
                                        line[0] = 0;
                                    }
                                    else
                                    {
                                        int real_idx = hist_curr % HIST_MAX;
                                        strcpy(line, history[real_idx]);
                                        i = strlen(line);
                                        print(line);
                                    }
                                }
                            }
                        }
                    }
                    continue;
                }

                if (i < 127)
                {
                    line[i++] = c;
                    char t[2] = {c, 0};
                    print(t);
                }
            }
        }
        line[i] = 0;
        if (i == 0)
        {
            continue;
        }

        // parse the cmd
        int argc = parse_cmd(line, argv);
        if (argc == 0)
        {
            continue;
        }

        // process the cmd

        /* --- HI --- */
        if (strncmp(argv[0], "hi", 2) == 0)
        {
            print("Hi! How are you doing?\n");
        }

        /* --- REBOOT --- */
        else if (strncmp(argv[0], "reboot", 6) == 0)
        {
            print("Rebooting...\n");
            reboot();
        }

        /* --- EXIT --- */
        else if (strncmp(argv[0], "exit", 4) == 0)
        {
            exit(0);
            break;
        }

        /* --- CLEAR --- */
        else if (strncmp(argv[0], "clear", 5) == 0)
        {
            print("\033[2J\033[1;1H");
        }

        /* --- LS --- */
        else if (strncmp(argv[0], "ls", 2) == 0)
        {
            char *list = (char *)malloc(512);
            list_files(list, 512);

            while (*list != 0)
            {
                print(list);
                print("\n");
                list += strlen(list) + 1;
            }
        }

        /* --- PWD --- */
        else if (strncmp(argv[0], "pwd", 3) == 0)
        {
            if (getcwd(cwd, 128))
            {
                print(cwd);
                print("\n");
            }
            else
            {
                print("Error: Cannot get current directory.\n");
            }
        }

        /* --- CD --- */
        else if (strncmp(argv[0], "cd", 2) == 0)
        {
            if (argc < 2)
            {
                print("Usage: cd <path>\n");
            }
            else
            {
                if (chdir(argv[1]) != 0)
                {
                    print("cd: no such file or directory: ");
                    print(argv[1]);
                    print("\n");
                }
            }
        }

        /* --- CAT --- */
        else if (strncmp(argv[0], "cat", 3) == 0)
        {
            if (argc < 2)
            {
                print("Usage: cat <filename>\n");
            }
            else
            {
                int fd = open(argv[1], O_RDONLY);
                if (fd < 0)
                {
                    print("cat: cannot open file: ");
                    print(argv[1]);
                    print("\n");
                }
                else
                {
                    char buf[64];
                    int n;
                    while ((n = read(fd, buf, 63)) > 0)
                    {
                        buf[n] = 0;
                        print(buf);
                    }
                    print("\n");
                    close(fd);
                }
            }
        }

        /* --- HELP --- */
        else if (strncmp(argv[0], "help", 4) == 0)
        {
            print("NyanOS Shell Commands:\n");
            print("  hi             Say hello\n");
            print("  reboot         Reboot the system\n");
            print("  exit           Exit the shell\n");
            print("  clear          Clear screen\n");
            print("  ls             List files (rootfs)\n");
            print("  pwd            Print working directory\n");
            print("  cd <path>      Change directory\n");
            print("  cat <file>     Print file contents\n");
            print("  help           Show this message\n");
            print("  <program>      Run executable (e.g. snake.elf)\n");
        }

        /* --- EXTERNAL PROGS --- */
        else
        {
            int pid = exec(argv[0], argv);
            if (pid < 0)
            {
                print("Command not found: ");
                print(argv[0]);
                print("\n");
            }
            else
            {
                int stat;
                waitpid(pid, &stat);
            }
        }
    }

    return 0;
}