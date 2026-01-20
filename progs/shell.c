#include "libc/libc.h"
#include "syscall_args.h"
#include "string.h"

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

int cmd_hi()
{
    print("Hi! How are you doing?\n");
}

int cmd_reboot()
{
    print("Rebooting...\n");
    reboot();
}

int cmd_exit()
{
    exit(0);
}

int cmd_clear()
{
    print("\033[2J\033[1;1H");
}

int cmd_ls()
{
    char *list = (char *)malloc(512);
    list_files(list, 512);

    char *curr_list = list;
    while (*curr_list != 0)
    {
        print(curr_list);
        print("\n");
        curr_list += strlen(curr_list) + 1;
    }

    free(list);
    return 0;
}

int cmd_pwd()
{
    char cwd[128];
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

int cmd_cd(int argc, char **argv)
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

int cmd_cat(int argc, char **argv)
{
    int fd = 0; // stdin is default

    if (argc >= 2)
    {
        fd = open(argv[1], O_RDONLY);
        if (fd < 0)
        {
            print("cat: cannot open file: ");
            print(argv[1]);
            print("\n");
            return 1;
        }
    }

    char buf[64];
    int n;
    while ((n = read(fd, buf, 63)) > 0)
    {
        buf[n] = 0;
        print(buf);
    }
    if (argc >= 2)
    {
        close(fd);
    }
    return 0;
}

int cmd_help()
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

int cmd_grep(int argc, char **argv)
{
    if (argc < 2)
    {
        print("Usage: grep <keyword>\n");
        return 1;
    }

    char *keyword = argv[1];
    char line_buf[256];
    int line_idx = 0;
    char c;
    int n;

    while ((n = read(0, &c, 1)) > 0)
    {
        if (c == '\n')
        {
            line_buf[line_idx] = 0;
            if (strstr(line_buf, keyword) != NULL)
            {
                print(line_buf);
                print("\n");
            }
            line_idx = 0;
            continue;
        }
        if (line_idx < 255)
        {
            line_buf[line_idx++] = c;
        }
    }

    if (line_idx > 0)
    {
        line_buf[line_idx] = 0;
        if (strstr(line_buf, keyword) != NULL)
        {
            print(line_buf);
            print("\n");
        }
    }

    return 0;
}

int exec_cmd(int argc, char **argv)
{
    /* --- HI --- */
    if (strncmp(argv[0], "hi", 3) == 0)
    {
        cmd_hi();
        return 1;
    }

    /* --- REBOOT --- */
    else if (strncmp(argv[0], "reboot", 7) == 0)
    {
        cmd_reboot();
        return 1;
    }

    /* --- EXIT --- */
    else if (strncmp(argv[0], "exit", 5) == 0)
    {
        cmd_exit();
        return 1;
    }

    /* --- CLEAR --- */
    else if (strncmp(argv[0], "clear", 6) == 0)
    {
        cmd_clear();
        return 1;
    }

    /* --- LS --- */
    else if (strncmp(argv[0], "ls", 3) == 0)
    {
        cmd_ls();
        return 1;
    }

    /* --- PWD --- */
    else if (strncmp(argv[0], "pwd", 4) == 0)
    {
        cmd_pwd();
        return 1;
    }

    /* --- CD --- */
    else if (strncmp(argv[0], "cd", 3) == 0)
    {
        cmd_cd(argc, argv);
        return 1;
    }

    /* --- CAT --- */
    else if (strncmp(argv[0], "cat", 4) == 0)
    {
        cmd_cat(argc, argv);
        return 1;
    }

    /* --- HELP --- */
    else if (strncmp(argv[0], "help", 5) == 0)
    {
        cmd_help();
        return 1;
    }

    /* --- GREP --- */
    else if (strncmp(argv[0], "grep", 5) == 0)
    {
        cmd_grep(argc, argv);
        return 1;
    }

    return 0;
}

int find_pipe(char **argv)
{
    int i = 0;
    while (1)
    {
        if (argv[i] == NULL)
        {
            return -1;
        }
        if (strncmp(argv[i], "|", 2) == 0)
        {
            return i;
        }
        i++;
    }
}

void run_pipe(char **left_argv, char **right_argv)
{
    int pipe_fd[2]; // just to remind myself, 0 is read_end, 1 is write_end :/

    if (pipe(pipe_fd) < 0)
    {
        print("Pipe failed\n");
        return;
    }

    // first, we fork to handle the writer on the left
    int pid1 = fork();
    if (pid1 == 0)
    {
        // CHILD 1: Writer

        close(pipe_fd[0]);   // close the reader, we don't need it for writing.
        dup2(pipe_fd[1], 1); // connect writer to the stdout
        close(pipe_fd[1]);   // safe to close the writer then, stdout now is the writer

        int argc = 0;
        while (left_argv[argc] != NULL)
        {
            argc++;
        }

        if (exec_cmd(argc, left_argv))
        {
            exit(0);
        }
        else
        {
            exec(left_argv[0], left_argv);
            print("Command not found: ");
            print(left_argv[0]);
            print("\n");
            exit(1);
        }
    }

    // then, we fork to handle the reader on the right
    int pid2 = fork();
    if (pid2 == 0)
    {
        // CHILD 2: Reader

        close(pipe_fd[1]);   // close the writer, we don't need it for reading.
        dup2(pipe_fd[0], 0); // connect reader to the stdin
        close(pipe_fd[0]);   // safe to close the reader then, stdin now is the reader

        int argc = 0;
        while (right_argv[argc] != NULL)
        {
            argc++;
        }

        if (exec_cmd(argc, right_argv))
        {
            exit(0);
        }
        else
        {
            exec(right_argv[0], right_argv);
            print("Command not found: ");
            print(right_argv[0]);
            print("\n");
            exit(1);
        }
    }

    close(pipe_fd[0]);
    close(pipe_fd[1]);

    int stat1 = 0;
    int stat2 = 0;
    waitpid(pid1, &stat1);
    waitpid(pid2, &stat2);
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
        int pipe_idx = find_pipe(argv);
        if (pipe_idx >= 0)
        {
            argv[pipe_idx] = NULL;
            char **right_argv = &argv[pipe_idx + 1];
            run_pipe(argv, right_argv);
            continue;
        }

        if (!exec_cmd(argc, argv))
        {
            /* --- EXTERNAL PROGS --- */
            int pid = fork();

            if (pid == 0)
            {
                exec(argv[0], argv);
                print("Command not found: ");
                print(argv[0]);
                print("\n");
                exit(1);
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