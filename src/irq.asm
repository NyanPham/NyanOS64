[BITS 64]
section .text

extern irq_entry

%macro DECLARE_IRQ 1
global irq%1_stub
irq%1_stub:
    push %1
    push rax
    mov rax, do_irq
    call rax
    pop rax
    add rsp, 8
    iretq
%endmacro

do_irq:
    push rbx
    push rdi
    push rsi
    push rdx
    push rcx
    push r8
    push r9
    push r10
    push r11

    mov rdi, rsp
    mov rsi, [rsp+80]
    call irq_entry

    pop r11
    pop r10
    pop r9
    pop r8
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbx

    ret

DECLARE_IRQ 0
DECLARE_IRQ 1
DECLARE_IRQ 2
DECLARE_IRQ 3
DECLARE_IRQ 4
DECLARE_IRQ 5
DECLARE_IRQ 6
DECLARE_IRQ 7
DECLARE_IRQ 8
DECLARE_IRQ 9
DECLARE_IRQ 10
DECLARE_IRQ 11
DECLARE_IRQ 12
DECLARE_IRQ 13
DECLARE_IRQ 14
DECLARE_IRQ 15