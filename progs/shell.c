#include "libc/libc.h"
#include "syscall_args.h"
#include "include/signal.h"
#include "string.h"

#define HIST_MAX 10
#define CMD_MAX_LEN 128

char history[HIST_MAX][CMD_MAX_LEN];
int hist_count = 0;
int hist_curr = 0;

static const char *builtin_cmds[] = {
    "hi", "reboot", "shutdown", "exit", "clear", "ls", "pwd", "cd",
    "cat", "grep", "echo", "rm", "touch", "cp", "mv", "mkdir",
    "ps", "free", "kill", "help", NULL};

void clear_curr_line(int len)
{
    for (int k = 0; k < len; k++)
    {
        print("\b \b");
    }
}

static void print_padded(const char *s, int width)
{
    print(s);
    int len = (int)strlen(s);
    for (int i = len; i < width; i++)
    {
        print(" ");
    }
}

static void print_int_padded(int num, int width)
{
    char buf[32];
    int i = 0;
    if (num == 0)
    {
        buf[i++] = '0';
    }
    else
    {
        int n = num;
        if (n < 0)
            n = -n;
        char tmp[16];
        int j = 0;
        while (n > 0)
        {
            tmp[j++] = (char)((n % 10) + '0');
            n /= 10;
        }
        if (num < 0)
            buf[i++] = '-';
        while (j > 0)
        {
            buf[i++] = tmp[--j];
        }
    }
    buf[i] = '\0';
    print_padded(buf, width);
}

static int parse_pid_str(const char *s)
{
    if (s == NULL || *s == '\0')
        return -1;
    int res = 0;
    for (int i = 0; s[i] != '\0'; i++)
    {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        res = res * 10 + (s[i] - '0');
    }
    return res;
}

int cmd_hi(void)
{
    print("Hi! How are you doing?\n");
    return 0;
}

int cmd_reboot(void)
{
    print("Rebooting...\n");
    reboot();
    return 0;
}

int cmd_exit(void)
{
    print("\x04");
    exit(0);
    return 0;
}

int cmd_clear(void)
{
    print("\033[32m\033[40m\033[2J\033[1;1H");
    return 0;
}

int cmd_ls(int argc, char **argv)
{
    char path[128];
    if (argc > 1)
    {
        strcpy(path, argv[1]);
    }
    else
    {
        if (getcwd(path, 128) == NULL)
        {
            print("ls: cannot get current directory\n");
            return 1;
        }
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        print("ls: cannot open directory '");
        print(path);
        print("'\n");
        return 1;
    }

    dirent_t entry;
    int idx = 0;

    while (readdir(fd, idx, &entry) == 0)
    {
        if (entry.type == VFS_DIRECTORY)
        {
            print("\033[36m[DIR ] \033[32m");
            print_padded(entry.name, 20);
            print("\n");
        }
        else
        {
            print("\033[37m[FILE] \033[32m");
            print_padded(entry.name, 20);
            print(" ");
            print_dec((int)entry.size);
            print(" B\n");
        }
        idx++;
    }

    close(fd);
    return 0;
}

int cmd_pwd(void)
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
    return 0;
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
    return 0;
}

int cmd_cat(int argc, char **argv)
{
    int fd = 0;

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

int cmd_ps(void)
{
    ProcInfo_t procs[64];
    int count = get_procs(procs, 64);
    if (count < 0)
    {
        print("ps: failed to query process list\n");
        return 1;
    }

    print("\033[36mPID   PPID  STATE     HEAP(KB)  WIN  NAME              CWD\033[32m\n");
    print("----------------------------------------------------------------\n");

    for (int i = 0; i < count; i++)
    {
        print_int_padded(procs[i].pid, 6);
        print_int_padded(procs[i].ppid, 6);

        const char *st_str = "UNKNOWN";
        switch (procs[i].state)
        {
        case 0:
            st_str = "READY";
            break;
        case 1:
            st_str = "WAITING";
            break;
        case 2:
            st_str = "DEAD";
            break;
        case 3:
            st_str = "ZOMBIE";
            break;
        case 4:
            st_str = "SLEEP";
            break;
        }
        print_padded(st_str, 10);
        print_int_padded((int)procs[i].heap_kb, 10);
        print_padded(procs[i].has_win ? "yes" : "no", 5);
        print_padded(procs[i].name, 18);
        print(procs[i].cwd);
        print("\n");
    }
    return 0;
}

int cmd_free(void)
{
    MemInfo_t mem;
    if (get_meminfo(&mem) < 0)
    {
        print("free: failed to read memory info\n");
        return 1;
    }

    uint64_t total_mb = mem.total_kb / 1024;
    uint64_t used_mb = mem.used_kb / 1024;
    uint64_t free_mb = mem.free_kb / 1024;
    int pct = (mem.total_pages > 0) ? (int)((mem.used_pages * 100) / mem.total_pages) : 0;

    print("\033[36mPhysical Memory (PMM) Usage:\033[32m\n");
    print("  Total : ");
    print_dec((int)mem.total_kb);
    print(" KB (");
    print_dec((int)total_mb);
    print(" MB, ");
    print_dec((int)mem.total_pages);
    print(" frames)\n");

    print("  Used  : ");
    print_dec((int)mem.used_kb);
    print(" KB (");
    print_dec((int)used_mb);
    print(" MB, ");
    print_dec(pct);
    print("%)\n");

    print("  Free  : ");
    print_dec((int)mem.free_kb);
    print(" KB (");
    print_dec((int)free_mb);
    print(" MB)\n");

    print("  [");
    int bars = (pct * 25) / 100;
    for (int i = 0; i < 25; i++)
    {
        if (i < bars)
            print("\033[33m#\033[32m");
        else
            print(".");
    }
    print("]\n");
    return 0;
}

int cmd_kill(int argc, char **argv)
{
    if (argc < 2)
    {
        print("Usage: kill <pid>\n");
        return 1;
    }

    int pid = parse_pid_str(argv[1]);
    if (pid <= 0)
    {
        print("kill: invalid PID (cannot kill kernel PID 0)\n");
        return 1;
    }

    if (kill_proc(pid, SIGKILL) < 0)
    {
        print("kill: no such process PID ");
        print_dec(pid);
        print("\n");
        return 1;
    }

    print("Killed process PID ");
    print_dec(pid);
    print("\n");
    return 0;
}

int cmd_help(void)
{
    print("NyanOS Shell Commands:\n");
    print("  ps             List running processes\n");
    print("  free           Display physical memory usage\n");
    print("  kill <pid>     Terminate a process by PID\n");
    print("  ls [path]      List files and directories\n");
    print("  pwd / cd <dir> Print or change working directory\n");
    print("  cat <file>     Print file contents\n");
    print("  grep <kw>      Filter lines containing keyword\n");
    print("  echo <txt>     Print text to terminal\n");
    print("  touch / rm     Create or remove a file\n");
    print("  cp / mv        Copy or move a file\n");
    print("  mkdir <dir>    Create a directory\n");
    print("  clear / exit   Clear screen or exit shell\n");
    print("  reboot / shutdown  Reboot or power off NyanOS\n");
    print("  <cmd1> | <cmd2> | <cmd3>  Multi-stage pipeline\n");
    print("  <cmd> < in > out >> app   Input/Output redirection\n");
    print("  [Tab]          Auto-complete commands & file paths\n");
    return 0;
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

int cmd_echo(int argc, char **argv)
{
    if (argc <= 1)
    {
        print("\n");
        return 0;
    }

    for (int i = 1; i < argc && argv[i] != NULL; i++)
    {
        print(argv[i]);
        if (i + 1 < argc && argv[i + 1] != NULL)
            print(" ");
    }
    print("\n");

    return argc - 1;
}

int cmd_rm(int argc, char **argv)
{
    if (argc <= 1)
    {
        print("No filename provided\n");
        return 0;
    }

    if (unlink(argv[1]) < 0)
    {
        print("Error to remove file\n");
        return -1;
    }

    return 0;
}

int cmd_touch(int argc, char **argv)
{
    if (argc < 2)
    {
        print("Usage: touch <filename>\n");
        return 1;
    }

    int fd = open(argv[1], O_CREAT | O_WRONLY);
    if (fd < 0)
    {
        print("[TOUCH]: cannot create file\n");
        return 1;
    }
    close(fd);
    return 0;
}

int cmd_cp(int argc, char **argv)
{
    if (argc < 3)
    {
        print("Usage: cp <source> <destination>\n");
        return 1;
    }

    int fd_src = open(argv[1], O_RDONLY);
    if (fd_src < 0)
    {
        print("[CP]: cannot open source file\n");
        return 1;
    }

    int fd_dst = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC);
    if (fd_dst < 0)
    {
        print("[CP]: cannot create destination file\n");
        close(fd_src);
        return 1;
    }

    char buf[512];
    int n;
    while ((n = read(fd_src, buf, 512)) > 0)
    {
        write(fd_dst, buf, n);
    }

    close(fd_src);
    close(fd_dst);
    print("[CP]: File copied successfully!\n");

    return 0;
}

int cmd_mkdir(int argc, char **argv)
{
    if (argc < 2)
    {
        print("Usage: mkdir <dirname>\n");
        return 1;
    }

    if (mkdir(argv[1]) < 0)
    {
        print("[MKDIR]: cannot create directory\n");
        return 1;
    }
    return 0;
}

int cmd_mv(int argc, char **argv)
{
    if (argc < 3)
    {
        print("Usage: mv <source> <destination>\n");
        return 1;
    }
    int fd_src = open(argv[1], O_RDONLY);
    if (fd_src < 0)
    {
        print("[MV]: cannot open source file\n");
        return 1;
    }

    int fd_dst = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC);
    if (fd_dst < 0)
    {
        print("[MV]: cannot create destination file\n");
        close(fd_src);
        return 1;
    }

    char buf[512];
    int n;
    while ((n = read(fd_src, buf, 512)) > 0)
    {
        write(fd_dst, buf, n);
    }
    close(fd_src);
    close(fd_dst);

    if (unlink(argv[1]) < 0)
    {
        print("[MV]: file copied, but failed to remove the source file\n");
        return 1;
    }

    return 0;
}

int cmd_shutdown(void)
{
    print("NyanOS is going to shutdown...Hope I'll see you again :(\n");
    shutdown();
    return 1;
}

int exec_cmd(int argc, char **argv)
{
    if (strcmp(argv[0], "hi") == 0)
    {
        cmd_hi();
        return 1;
    }
    else if (strcmp(argv[0], "reboot") == 0)
    {
        cmd_reboot();
        return 1;
    }
    else if (strcmp(argv[0], "exit") == 0)
    {
        cmd_exit();
        return 1;
    }
    else if (strcmp(argv[0], "clear") == 0)
    {
        cmd_clear();
        return 1;
    }
    else if (strcmp(argv[0], "ls") == 0)
    {
        cmd_ls(argc, argv);
        return 1;
    }
    else if (strcmp(argv[0], "pwd") == 0)
    {
        cmd_pwd();
        return 1;
    }
    else if (strcmp(argv[0], "cd") == 0)
    {
        cmd_cd(argc, argv);
        return 1;
    }
    else if (strcmp(argv[0], "cat") == 0)
    {
        cmd_cat(argc, argv);
        return 1;
    }
    else if (strcmp(argv[0], "ps") == 0)
    {
        cmd_ps();
        return 1;
    }
    else if (strcmp(argv[0], "free") == 0)
    {
        cmd_free();
        return 1;
    }
    else if (strcmp(argv[0], "kill") == 0)
    {
        cmd_kill(argc, argv);
        return 1;
    }
    else if (strcmp(argv[0], "help") == 0)
    {
        cmd_help();
        return 1;
    }
    else if (strcmp(argv[0], "grep") == 0)
    {
        cmd_grep(argc, argv);
        return 1;
    }
    else if (strcmp(argv[0], "echo") == 0)
    {
        cmd_echo(argc, argv);
        return 1;
    }
    else if (strcmp(argv[0], "rm") == 0)
    {
        cmd_rm(argc, argv);
        return 1;
    }
    else if (strcmp(argv[0], "touch") == 0)
    {
        cmd_touch(argc, argv);
        return 1;
    }
    else if (strcmp(argv[0], "cp") == 0)
    {
        cmd_cp(argc, argv);
        return 1;
    }
    else if (strcmp(argv[0], "mkdir") == 0)
    {
        cmd_mkdir(argc, argv);
        return 1;
    }
    else if (strcmp(argv[0], "mv") == 0)
    {
        cmd_mv(argc, argv);
        return 1;
    }
    else if (strcmp(argv[0], "shutdown") == 0)
    {
        cmd_shutdown();
        return 1;
    }

    return 0;
}

static void exec_external_with_path(char **argv)
{
    exec(argv[0], argv);
    if (strchr(argv[0], '/') == NULL)
    {
        char bin_path[128] = "/bin/";
        int k = 5;
        for (int i = 0; argv[0][i] != '\0' && k < 126; i++)
        {
            bin_path[k++] = argv[0][i];
        }
        bin_path[k] = '\0';
        exec(bin_path, argv);
    }
    print("Command not found: ");
    print(argv[0]);
    print("\n");
    exit(1);
}

static int find_char(char **argv, char *c)
{
    int i = 0;
    while (argv[i] != NULL)
    {
        if (strcmp(argv[i], c) == 0)
        {
            return i;
        }
        i++;
    }
    return -1;
}

static void setup_redirections(char **argv, int *out_argc, int *saved_stdin, int *saved_stdout)
{
    *saved_stdin = -1;
    *saved_stdout = -1;

    int in_idx = find_char(argv, "<");
    if (in_idx >= 0 && argv[in_idx + 1] != NULL)
    {
        char *in_file = argv[in_idx + 1];
        int fd = open(in_file, O_RDONLY);
        if (fd < 0)
        {
            print("Shell: Cannot open input file: ");
            print(in_file);
            print("\n");
        }
        else
        {
            *saved_stdin = 9;
            dup2(0, *saved_stdin);
            dup2(fd, 0);
            close(fd);
        }
        int j = in_idx;
        while (argv[j + 2] != NULL)
        {
            argv[j] = argv[j + 2];
            j++;
        }
        argv[j] = NULL;
        *out_argc -= 2;
    }

    int append_idx = find_char(argv, ">>");
    int trunc_idx = find_char(argv, ">");
    int redir_idx = -1;
    int mode = O_WRONLY | O_CREAT;

    if (append_idx >= 0)
    {
        redir_idx = append_idx;
        mode |= O_APPEND;
    }
    else if (trunc_idx >= 0)
    {
        redir_idx = trunc_idx;
        mode |= O_TRUNC;
    }

    if (redir_idx >= 0 && argv[redir_idx + 1] != NULL)
    {
        char *out_file = argv[redir_idx + 1];
        int fd = open(out_file, mode);
        if (fd < 0)
        {
            print("Shell: Cannot open output file: ");
            print(out_file);
            print("\n");
        }
        else
        {
            *saved_stdout = 10;
            dup2(1, *saved_stdout);
            dup2(fd, 1);
            close(fd);
        }
        argv[redir_idx] = NULL;
        *out_argc = redir_idx;
    }
}

static void restore_redirections(int saved_stdin, int saved_stdout)
{
    if (saved_stdin != -1)
    {
        dup2(saved_stdin, 0);
        close(saved_stdin);
    }
    if (saved_stdout != -1)
    {
        dup2(saved_stdout, 1);
        close(saved_stdout);
    }
}

void run_pipeline(char **argv)
{
    char **stages[8];
    int num_stages = 0;

    stages[num_stages++] = &argv[0];
    for (int i = 0; argv[i] != NULL; i++)
    {
        if (strcmp(argv[i], "|") == 0)
        {
            argv[i] = NULL;
            if (num_stages < 8 && argv[i + 1] != NULL)
            {
                stages[num_stages++] = &argv[i + 1];
            }
        }
    }

    int pids[8];
    int prev_read_fd = -1;

    for (int s = 0; s < num_stages; s++)
    {
        int pipe_fd[2] = {-1, -1};
        if (s < num_stages - 1)
        {
            if (pipe(pipe_fd) < 0)
            {
                print("Pipeline: pipe() failed\n");
                break;
            }
        }

        int pid = fork();
        if (pid == 0)
        {
            if (prev_read_fd != -1)
            {
                dup2(prev_read_fd, 0);
                close(prev_read_fd);
            }
            if (pipe_fd[0] != -1)
            {
                close(pipe_fd[0]);
            }
            if (pipe_fd[1] != -1)
            {
                dup2(pipe_fd[1], 1);
                close(pipe_fd[1]);
            }

            char **st_argv = stages[s];
            int st_argc = 0;
            while (st_argv[st_argc] != NULL)
            {
                st_argc++;
            }

            int s_in = -1, s_out = -1;
            setup_redirections(st_argv, &st_argc, &s_in, &s_out);

            if (exec_cmd(st_argc, st_argv))
            {
                exit(0);
            }
            else
            {
                exec_external_with_path(st_argv);
            }
        }

        pids[s] = pid;
        if (prev_read_fd != -1)
        {
            close(prev_read_fd);
        }
        if (pipe_fd[1] != -1)
        {
            close(pipe_fd[1]);
        }
        prev_read_fd = pipe_fd[0];
    }

    if (prev_read_fd != -1)
    {
        close(prev_read_fd);
    }

    for (int s = 0; s < num_stages; s++)
    {
        int stat = 0;
        waitpid(pids[s], &stat);
    }
}

static void handle_tab_completion(char *line, int *len_ptr)
{
    int len = *len_ptr;
    line[len] = '\0';

    int tok_start = len;
    while (tok_start > 0 && line[tok_start - 1] != ' ')
    {
        tok_start--;
    }

    char *tok = &line[tok_start];
    int is_first_word = 1;
    for (int i = 0; i < tok_start; i++)
    {
        if (line[i] != ' ')
        {
            is_first_word = 0;
            break;
        }
    }

    char dir_path[128];
    const char *prefix = tok;
    int last_slash = -1;
    for (int i = 0; tok[i] != '\0'; i++)
    {
        if (tok[i] == '/')
            last_slash = i;
    }

    if (last_slash >= 0)
    {
        if (last_slash == 0)
        {
            strcpy(dir_path, "/");
        }
        else
        {
            strncpy(dir_path, tok, (size_t)last_slash);
            dir_path[last_slash] = '\0';
        }
        prefix = &tok[last_slash + 1];
    }
    else
    {
        if (getcwd(dir_path, 128) == NULL)
            strcpy(dir_path, "/");
    }

    size_t prefix_len = strlen(prefix);
    char match[128] = {0};
    int match_is_dir = 0;
    int match_count = 0;

    if (is_first_word && last_slash == -1)
    {
        for (int b = 0; builtin_cmds[b] != NULL; b++)
        {
            if (strncmp(builtin_cmds[b], prefix, prefix_len) == 0)
            {
                if (match_count == 0)
                    strcpy(match, builtin_cmds[b]);
                match_count++;
            }
        }

        int bin_fd = open("/bin", O_RDONLY);
        if (bin_fd >= 0)
        {
            dirent_t ent;
            int idx = 0;
            while (readdir(bin_fd, idx++, &ent) == 0)
            {
                if (strncmp(ent.name, prefix, prefix_len) == 0)
                {
                    if (match_count == 0)
                    {
                        strcpy(match, ent.name);
                        match_is_dir = (ent.type == VFS_DIRECTORY);
                    }
                    match_count++;
                }
            }
            close(bin_fd);
        }
    }

    int dir_fd = open(dir_path, O_RDONLY);
    if (dir_fd >= 0)
    {
        dirent_t ent;
        int idx = 0;
        while (readdir(dir_fd, idx++, &ent) == 0)
        {
            if (strcmp(ent.name, ".") == 0 || strcmp(ent.name, "..") == 0)
                continue;
            if (strncmp(ent.name, prefix, prefix_len) == 0)
            {
                if (match_count == 0)
                {
                    strcpy(match, ent.name);
                    match_is_dir = (ent.type == VFS_DIRECTORY);
                }
                match_count++;
            }
        }
        close(dir_fd);
    }

    if (match_count == 1)
    {
        const char *suffix = &match[prefix_len];
        while (*suffix && *len_ptr < 125)
        {
            char ch = *suffix++;
            line[(*len_ptr)++] = ch;
            char s[2] = {ch, 0};
            print(s);
        }
        char tail = match_is_dir ? '/' : ' ';
        if (*len_ptr < 126)
        {
            line[(*len_ptr)++] = tail;
            char s[2] = {tail, 0};
            print(s);
        }
        line[*len_ptr] = '\0';
    }
}

int parse_cmd(char *line, char **argv)
{
    int argc = 0;
    int in_tok = 0;

    while (*line)
    {
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

int main(void)
{
    char line[128];
    char cwd[128];
    char *argv[32];

    print("\033[32m\033[40m");
    print("\033[2J\033[1;1H");
    print("Welcome to NyanOS Shell!\n");
    print("Type 'help' for commands (ps, free, kill, ls, cd, cat, grep, pipes, Tab-complete).\n\n");

    while (1)
    {
        if (getcwd(cwd, 128) != NULL)
        {
            print("\033[33mNyanOS ");
            print(cwd);
            print("> \033[32m");
        }
        else
        {
            print("\033[33mNyanOS ???> \033[32m");
        }

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
                    line[i] = 0;
                    if (i > 0)
                    {
                        int nxt_slot = hist_count % HIST_MAX;
                        strcpy(history[nxt_slot], line);
                        hist_count++;
                    }
                    break;
                }

                if (c == '\x03')
                {
                    print("^C\n");
                    i = 0;
                    line[0] = 0;
                    break;
                }

                if (c == '\t')
                {
                    handle_tab_completion(line, &i);
                    continue;
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
                            if (seq[1] == 'A')
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
                                        i = (int)strlen(line);
                                        print(line);
                                    }
                                }
                            }
                            else if (seq[1] == 'B')
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
                                        i = (int)strlen(line);
                                        print(line);
                                    }
                                }
                            }
                        }
                    }
                    continue;
                }

                if (c == '\x0C')
                {
                    print("\033[0m\033[2J\033[1;1H");
                    if (getcwd(cwd, 128) != NULL)
                    {
                        print("\033[33mNyanOS ");
                        print(cwd);
                        print("> \033[32m");
                    }
                    else
                    {
                        print("\033[33mNyanOS ???> \033[32m");
                    }

                    line[i] = 0;
                    print(line);
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

        int argc = parse_cmd(line, argv);
        if (argc == 0)
        {
            continue;
        }

        if (find_char(argv, "|") >= 0)
        {
            run_pipeline(argv);
            continue;
        }

        int amper_idx = find_char(argv, "&");
        if (amper_idx != -1)
        {
            argv[amper_idx] = NULL;
            argc = amper_idx;
        }

        int saved_stdin = -1;
        int saved_stdout = -1;
        setup_redirections(argv, &argc, &saved_stdin, &saved_stdout);

        if (argc > 0 && !exec_cmd(argc, argv))
        {
            int pid = fork();

            if (pid == 0)
            {
                exec_external_with_path(argv);
            }
            else
            {
                if (amper_idx == -1)
                {
                    set_fg(pid);
                    int stat;
                    waitpid(pid, &stat);
                    set_fg(-1);
                }
                else
                {
                    set_fg(-1);
                }
            }
        }

        restore_redirections(saved_stdin, saved_stdout);
    }

    return 0;
}