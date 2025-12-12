[BITS 64]

global _start

section .bss
    input_buf:   resb 64  
    buf_idx:     resq 1
    file_content: resb 2048

section .data
    prompt: db "NyanOS> ", 0
    msg_hi: db "Hi! How are you doing?", 0x0A, 0
    msg_unknown: db "I don't understand that :(", 0x0A, 0
    newline: db 0x0A, 0

    cmd_hi: db "hi", 0
    cmd_clear: db "clear", 0
    cmd_reboot: db "reboot", 0
    cmd_ls: db "ls", 0
    cmd_cat: db "cat ", 0
    cmd_exec: db "exec ", 0
    msg_file_not_found: db "Error: file not found!", 0x0A, 0
    bs_char: db 0x08, 0

section .text
_start:
    mov rax, 1
    mov rdi, prompt
    mov rsi, 0x00FF00   ; green
    syscall

    mov qword [buf_idx], 0

.read_loop:
    mov rax, 2
    syscall

    cmp al, 0x0A
    je .process_cmd

    cmp al, 0x08
    je .handle_backspace

    mov rbx, [buf_idx]
    mov byte [input_buf + rbx], al
    mov byte [input_buf + rbx + 1], 0
    inc qword[buf_idx]

    mov rax, 1
    lea rdi, [input_buf + rbx]
    mov rsi, 0xFFFFFF   ; white
    syscall

    jmp .read_loop

.handle_backspace:
    mov rbx, [buf_idx]
    cmp rbx, 0
    je .read_loop

    dec qword [buf_idx]
    mov rbx, [buf_idx]
    mov byte [input_buf + rbx], 0

    mov rax, 1
    mov rdi, bs_char
    mov rsi, 0xFFFFFF
    syscall 

    jmp .read_loop

.process_cmd:
    mov rbx, [buf_idx]
    mov byte [input_buf + rbx], 0
    
    mov rax, 1
    mov rdi, newline
    syscall

.cmp_cmd_hi:
    mov rdi, cmd_hi
    mov rsi, input_buf
    mov rdx, 2
    call compare_cmd

    test rax, rax
    jne .cmp_cmd_clear

    mov rax, 1
    mov rdi, msg_hi
    mov rsi, 0xFFFF00   ; yellow
    syscall
    jmp _start

.cmp_cmd_clear:
    mov rdi, cmd_clear
    mov rsi, input_buf
    mov rdx, 5
    call compare_cmd

    test rax, rax
    jne .cmp_cmd_reboot

    mov rax, 3
    syscall
    jmp _start

.cmp_cmd_reboot:
    mov rdi, cmd_reboot
    mov rsi, input_buf
    mov rdx, 6
    call compare_cmd

    test rax, rax
    jne .cmp_cmd_ls

    mov rax, 4
    syscall
    jmp _start

.cmp_cmd_ls:
    mov rdi, cmd_ls
    mov rsi, input_buf
    mov rdx, 2
    call compare_cmd

    test rax, rax
    jne .cmp_cmd_cat

    mov rax, 5
    syscall
    jmp _start

.cmp_cmd_cat:
    mov rdi, cmd_cat
    mov rsi, input_buf
    mov rdx, 4
    call starts_with

    test rax, rax
    jne .cmp_cmd_exec

    mov rax, 10
    lea rdi, [input_buf+4]
    mov rsi, 0
    syscall

    cmp rax, 0
    jl .cat_fail

    mov rbx, rax
    
.cat_read_loop:
    mov rax, 12
    mov rdi, rbx
    lea rsi, [file_content] 
    mov r14, 2047 ; 1 last byte for \0
    syscall 

    cmp rax, 0
    jle .cat_close 

    lea r8, [file_content]
    add r8, rax
    mov byte[r8], 0

    mov rax, 1
    lea rdi, [file_content]
    mov rsi, 0x00FFFF ; cyan
    syscall

    jmp .cat_close
.cat_close:
    mov rax, 11
    mov rdi, rbx
    syscall

    mov rax, 1
    mov rdi, newline
    syscall 

    jmp _start

.cat_fail:
    mov rax, 1
    mov rdi, msg_file_not_found
    mov rsi, 0xFF0000   ; red
    syscall

    jmp _start

.cmp_cmd_exec:
    mov rdi, cmd_exec
    mov rsi, input_buf
    mov rdx, 5
    call starts_with

    test rax, rax
    jne .unknown_cmd

    mov rax, 7
    lea rdi, [input_buf+5] ; fname
    syscall

    test rax, rax
    jns .exec_succ

    mov rax, 1
    mov rdi, msg_file_not_found
    mov rsi, 0xFF0000
    syscall
    jmp _start

.exec_succ:
    mov rdi, rax
    xor rsi, rsi
    mov rax, 9
    syscall

    jmp _start

.unknown_cmd:
    mov rax, 1 
    mov rdi, msg_unknown
    mov rsi, 0xFF0000   ; red
    syscall
    jmp _start

;============================================
; compare_cmd(cmd, input, len) -> 0 if equal
;============================================
compare_cmd:
    push rbp
    mov rbp, rsp
    push rbx

    mov rcx, rdx
    xor rdx, rdx
    jecxz .done

.cmp_char:
    mov al, [rdi]
    mov bl, [rsi]
    cmp al, bl
    jne .diff
    inc rdi
    inc rsi
    loop .cmp_char
    cmp byte [rsi], 0
    je  .done
    
.diff:
    inc rdx
.done:
    mov rax, rdx
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

;============================================
; starts_with(cmd, input, len) -> 0 if equal
;============================================
starts_with:
    push rbp
    mov rbp, rsp
    push rbx

    mov rcx, rdx
    xor rdx, rdx
    jecxz .done

.cmp_char:
    mov al, [rdi]
    mov bl, [rsi]
    cmp al, bl
    jne .diff
    inc rdi
    inc rsi
    loop .cmp_char
    jmp .done
    
.diff:
    inc rdx
.done:
    mov rax, rdx
    pop rbx
    mov rsp, rbp
    pop rbp
    ret