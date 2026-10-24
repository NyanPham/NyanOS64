[BITS 64]

global _start
extern main

section .text

_start:
    pop rdi
    mov rsi, rsp
    and rsp, -16

    call main

    mov rdi, rax    
    mov rax, 22      ; call sys_exit, with the exit code from main
    syscall

    jmp $

section .note.GNU-stack noalloc noexec nowrite progbits