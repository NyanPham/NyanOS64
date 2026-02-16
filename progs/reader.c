#include "libc/libc.h"

void main()
{
    print("Reader: Opening SHM 'myshm'...\n");

    int fd = shm_open("myshm", 0, 0);
    if (fd < 0)
    {
        print("Reader: Fail to open SHM. You ran writer before??\n");
        exit(1);
    }
    print("Reader: SHM open OK\n");

    stat_t st;
    if (fstat(fd, &st) < 0)
    {
        print("Reader: Fail to fstat\n");
        close(fd);
        exit(1);
    }

    if (S_ISCHR(st.st_mode))
    {
        kprint("SHM Detected! Size: ");
        kprint_int(st.st_size);
        kprint("\n");

        char *ptr = (char *)mmap(NULL, st.st_size, 0, 0, fd, 0);
        if (ptr == NULL)
        {
            print("Reader: Fail to mmap\n");
            exit(1);
        }
        print("Reader: Mmap OK\n");

        print("Reader: Content from SHM:\n");
        print("-----------------------------\n");
        print(ptr);
        print("\n-----------------------------\n");

        munmap(ptr, 4096);

        exit(0);
    }

    print("Not a SHM device\n");
    close(fd);

    exit(1);
}