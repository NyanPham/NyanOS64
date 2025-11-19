[BITS 64]

global _start

section .text
_start:
    mov rax, 0xDEADBEEF
    jmp $
