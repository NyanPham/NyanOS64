[BITS 64]

section .bss
    usr_stk_tmp: resq 1
    kern_stk_ptr: resq 1

extern syscall_handler
global kern_stk_ptr
global syscall_entry

section .text

;===========================
; syscall_entry:
;   in: 
;       rax - sys_num
;       rdi - arg1
;       rsi - arg2
;       rdx - arg3
;       rcx - ret_addr (set by CPU)
;       r11 - rflags (set by CPU)
;===========================
syscall_entry:
    ;   swap the user stack with the kernel stack manually
    mov [usr_stk_tmp], rsp
    mov rsp, [kern_stk_ptr]
    
    push qword [usr_stk_tmp]

    ; save the context (CPU-saved and Caller-saved regs)
    ; CPU saved regs
    push rcx
    push r11

    ; and caller-saved regs
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; shuffle the registers from user-space to System V ABI
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax
    call syscall_handler

    ; restore the context
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx 
    pop rbp

    pop r11
    pop rcx

    pop rsp
    o64 sysret