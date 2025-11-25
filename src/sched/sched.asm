[BITS 64]
global switch_to_task

;======================================================
; void switch_to_task(prev_rsp_ptr, next_rsp)
;======================================================
switch_to_task:
    ; first, we push the callee-saved registers
    push R15
    push R14
    push R13
    push R12
    push RBP
    push RBX

    ; second, we do the stack swap
    mov qword [rdi], rsp    ; save the old rsp into [prev_rsp_ptr]
    mov rsp, rsi            ; store new rsp

    ; finally, restore the new task and jmp to it
    pop RBX
    pop RBP
    pop R12
    pop R13
    pop R14
    pop R15

    ret

global task_start_stub
task_start_stub:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    mov ax, 0x2B
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    iretq