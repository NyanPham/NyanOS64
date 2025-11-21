[BITS 64]

section .data
    msg_hello: db "Hello World from User Space (ring 3) via syscall", 0x0A, 0
    key_buf: times 8 db 0


global _start

section .text
_start:
    mov rax, 1
    mov rsi, msg_hello
    syscall
    
    mov rax, 0
    syscall

    call _get_key_loop

    jmp $

_get_key_loop:
    mov rax, 2
    syscall
    mov [key_buf], rax

    mov rax, 1
    mov rsi, key_buf
    syscall

    jmp _get_key_loop