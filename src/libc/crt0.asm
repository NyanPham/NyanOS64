[BITS 64]

global _start
extern main

section .text

_start:
    pop rdi
    mov rsi, rsp

    call main

    mov rdi, rax    
    mov rax, 8      ; call sys_exit, with the exit code from main
    syscall

    jmp $