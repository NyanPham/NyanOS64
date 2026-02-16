#include "libc/libc.h"

#define SHM_SIZE 4096

void main()
{
    print("Writer: Creating SHM 'myshm'...\n");

    int fd = shm_open("myshm", O_CREAT | O_RDWR, 0);
    if (fd < 0)
    {
        print("Writer: Fail to open SHM\n");
        exit(1);
    }
    print("Writer: SHM open OK\n");

    if (ftruncate(fd, SHM_SIZE) < 0)
    {
        print("Writer: Fail to truncate\n");
        exit(1);
    }
    print("Writer: Truncate OK\n");

    char *ptr = (char *)mmap(NULL, SHM_SIZE, 0, 0, fd, 0);
    if (ptr == NULL)
    {
        print("Writer: Fail to mmap\n");
        exit(1);
    }
    print("Writer: Mmap OK\n");

    strcpy(ptr, "Hello Shared Memory from NyanOS!");

    print("Writer: Wrote message to SHM.\n");
    print("Writer: Done. Now run reader.elf!\n");

    exit(0);
}