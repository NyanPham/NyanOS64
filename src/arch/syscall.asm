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
;   out: 
;       rax - ret_val
;===========================
syscall_entry:
    ;   swap the user stack with the kernel stack manually
    cli
    mov [usr_stk_tmp], rsp
    mov rsp, [kern_stk_ptr]
    
    push qword [usr_stk_tmp]
    sti

    ; save the context (CPU-saved and Caller-saved regs)
    ; CPU saved regs
    push rcx
    push r11

    ; and caller-saved regs
    push rax
    push rbp
    push rbx
    push r8
    push r9
    push r10
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
    mov [rsp + 72], rax ; sizeof(uint64_t) * 9 registers to the push rax above

    ; restore the context
    pop r15
    pop r14
    pop r13
    pop r12
    pop r10
    pop r9
    pop r8
    pop rbx 
    pop rbp
    pop rax

    pop r11
    pop rcx

    pop rsp
    o64 sysret