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
;       rcx - ret_addr
;       r11 - rflags
;===========================
syscall_entry:
    ;   swap the user stack with the kernel stack manually
    mov [usr_stk_tmp], rsp
    mov rsp, [kern_stk_ptr]
    
    push qword [usr_stk_tmp]

    push rcx
    push r11

    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov rdi, rax
    call syscall_handler

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