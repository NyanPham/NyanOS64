[BITS 64]
global switch_to_task

;======================================================
; void switch_to_task(prev_rsp_ptr, next_rsp, prev_fpu, next_fpu)
;======================================================
switch_to_task:
    ; first, we check if the prev_rsp_ptr is null
    ; if null, means the prev task dies, we don't
    ; care its stacks
    test rdi, rdi
    jz .skip_save

    ; first, we push the callee-saved registers
    push r15
    push r14
    push r13
    push r12
    push rbp
    push rbx

    ; second, we do the stack swap
    mov qword [rdi], rsp    ; save the old rsp into [prev_rsp_ptr]

    ; third, save the FPU/SSE
    test rdx, rdx
    jz .skip_save_fpu
    fxsave [rdx]            ; save 512 bytes into prev_fpu buffer

.skip_save_fpu:
.skip_save:
    mov rsp, rsi            ; store new rsp

    ; fourth, check if we can restore FPU/SSE
    test rcx, rcx
    jz .skip_load_fpu
    fxrstor [rcx]

.skip_load_fpu:
    ; finally, restore the new task and jmp to it
    pop rbx
    pop rbp
    pop r12
    pop r13
    pop r14
    pop r15

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

    mov ax, 0x2B
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    pop rax

    iretq