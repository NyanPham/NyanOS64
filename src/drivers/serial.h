#ifndef SERIAL_H 
#define SERIAL_H 

#include <stdint.h>

void serial_write(char c);
void kprint(const char* str);
void kprint_hex_64(uint64_t val);
void kprint_hex_32(uint32_t val);
void kprint_int(int val);
void serial_init();

#endif