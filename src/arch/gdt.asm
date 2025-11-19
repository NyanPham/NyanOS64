global gdt_set

section .text

;========================
; gdt_set
; Inputs:
;   - gdt ptr
;========================
gdt_set:
    lgdt [rdi]

    ; reload segment registers
    push 0x08
    lea rax, [rel .reload_cs]
    push rax
    retfq
.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    ret

global enter_user_mode

section .text

;==================================
; void enter_user_mode(entry, usr_stk_ptr)
; 
; the subroutine will configure the data segments
; for user (ds, es, fs, and gs).
; it creates stack frame for iretq 
;==================================
enter_user_mode:
    cli

    push 0x33       ; user data segment
    push rsi        ; usr_stk_ptr
    push 0x202      ; Rflags, IF=1, bit 1 = 1
    push 0x2B       ; user code segment 0x28 | RPL 3
    push rdi        ; entry

    mov ax, 0x33    ; user data selector 0x30 | RPL 3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    iretq