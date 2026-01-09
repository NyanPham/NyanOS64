#ifndef LIBC_H
#define LIBC_H

#include "syscall_args.h"

#include <stdint.h>
#include <stddef.h>

#define O_RDONLY 0x00
#define O_WRONLY 0x01
#define O_RDWR 0x02

void exit(int status);
void print(const char *str);
void kprint(const char *s);
void kprint_int(int x);
int open(const char *pathname, uint32_t flags);
int close(int fd);
int read(int fd, void *buf, uint64_t count);
void reboot(void);

char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
int strncmp(const char *s1, const char *s2, size_t n);
size_t strlen(const char *s);
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

/* Heap manager */
void *sbrk(int64_t incr_payload);
void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);

/* Rand */
int rand(void);
void srand(unsigned int seed);

/* Dir sys */
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
void list_files(char *list, uint64_t max_len);

/* Process/syscall */
int exec(const char *path, char *const argv[]);
int waitpid(int pid, int *status);

/* Others */
void move_cursor(int row, int col);
int get_key(void);
void sleep(uint64_t loop_cnt);
int win_create(WinParams_t *win_params);
int create_term(int x, int y, uint32_t w, uint32_t h, const char *title, uint32_t win_flags);

#endif