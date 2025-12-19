#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#define SYS_KPRINT 13

void syscall_init(void);
uint64_t syscall_handler(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);
void resolve_path(const char* cwd, const char* inp_path, char* out_buf);

#endif