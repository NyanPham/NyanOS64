#ifndef LIBC_H
#define LIBC_H

#include <stdint.h>
#include <stddef.h>

#define O_RDONLY 0x00
#define O_WRONLY 0x01
#define O_RDWR 0x02

void exit(int status);
void print(const char* str);
void kprint(const char* s);
int open(const char* pathname, uint32_t flags);
int close(int fd);
int read(int fd, void* buf, uint64_t count);

char* strcpy(char* dest, const char* src);
int strncmp(const char* s1, const char* s2, size_t n); 
size_t strlen(const char* s);
void* memset(void* s, int c, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);

/* Heap manager */
void* sbrk(int64_t incr_payload);
void* malloc(size_t size);
void free(void* ptr);
void* realloc(void* ptr, size_t size);

#endif