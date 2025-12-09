#ifndef LIBC_H
#define LIBC_H

#include <stdint.h>
#include <stddef.h>

#define O_RDONLY 0x00
#define O_WDONLY 0x01
#define O_RDWR 0x02

void exit(int status);
void print(const char* str, uint32_t color);
int open(const char* pathname, uint32_t flags);
int close(int fd);
int read(int fd, void* buf, uint64_t count);

int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n); 
size_t strlen(const char* s);
void* memset(void* s, int c, size_t n);

#endif