#ifndef LIBC_H
#define LIBC_H

#include <stdint.h>

void exit(int status);
void print(const char* str, uint32_t color);

#endif