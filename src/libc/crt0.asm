[BITS 64]

global _start
extern main

section .text

_start:
    ; NyanOS kernel not supports args, yet
    ; so let's pretend argc = 0, and argv = NULL
    xor rdi, rdi
    xor rsi, rsi

    call main

    mov rdi, rax    
    mov rax, 8      ; call sys_exit, with the exit code from main
    syscall

    jmp $