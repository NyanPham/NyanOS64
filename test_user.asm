[BITS 64]

global _start

section .text
_start:
    mov rax, 0          
    mov rsi, 0x1234     
    syscall
    jmp $