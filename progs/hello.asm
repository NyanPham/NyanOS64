[BITS 64]

global _start

section .data
    msg: db "Hello from a separate program :))", 0x0A, 0

section .text
_start:
    mov rax, 1
    mov rsi, msg
    mov rdx, 0x00FFFF   ; Cyan
    syscall

    mov rax, 8
    syscall

    jmp $


