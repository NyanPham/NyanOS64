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


