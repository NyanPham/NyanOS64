[BITS 64]
section .text

extern irq_entry

; This macro creates a stub for each IRQ.
; The stub pushes the IRQ number onto the stack and then calls the common IRQ handler.
%macro DECLARE_IRQ 1
global irq%1_stub
irq%1_stub:
    push %1     ; Push the IRQ number.
    push rax    ; Push a dummy value for alignment.
    mov rax, do_irq     ; Call the common IRQ handler.
    call rax
    pop rax     ; Pop the dummy value.
    add rsp, 8  ; Pop the IRQ number.
    
    iretq       ; Return from the interrupt.
%endmacro

; This is the common IRQ handler.
; It saves the registers, calls the C-level IRQ entry function, and then restores the registers.
do_irq:
    ; Save all general-purpose registers.
    push rbx
    push rdi
    push rsi
    push rdx
    push rcx
    push r8
    push r9
    push r10
    push r11

    ; The stack pointer is the first argument to irq_entry.
    ; The IRQ number is the second argument.
    ; How come we get rsp + 0x58? do_irq pushed 9 regs, 
    ; and its caller, irq%1_stub has stack layout as ret_addr, dummy value, and IRQ number.
    ; we want to stop at the IRQ number, so offset = 9 * 8 + 8 + 8 = 0x58.

    mov rdi, rsp    
    mov rsi, [rsp+0x58] 
    call irq_entry  ; Call the C-level IRQ entry function.

    ; Restore all general-purpose registers.
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

; Declare the IRQ stubs.
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