[BITS 64]

global _start

section .bss
    input_buf:   resb 64  
    buf_idx:     resq 1
    file_content: resb 2048
    argv_ptrs: resq 16 ; max 16 arg ptrs

section .data
    prompt: db "NyanOS> ", 0
    prompt_len equ $ - prompt

    msg_hi: db "Hi! How are you doing?", 0x0A, 0
    msg_hi_len equ $ - msg_hi

    msg_unknown: db "I don't understand that :(", 0x0A, 0
    msg_unknown_len equ $ - msg_unknown

    newline: db 0x0A, 0
    newline_len equ $ - newline

    cmd_hi: db "hi", 0
    cmd_clear: db "clear", 0
    cmd_reboot: db "reboot", 0
    cmd_ls: db "ls", 0
    cmd_cat: db "cat ", 0
    cmd_exec: db "exec ", 0

    msg_file_not_found: db "Error: file not found!", 0x0A, 0
    msg_file_not_found_len equ $ - msg_file_not_found
    
    bs_char: db 0x08, 0
    bs_char_len equ $ - bs_char

section .text
_start:
    mov rax, 1              ; sys_write
    mov rdi, 1              ; fd = stdout
    mov rsi, prompt         ; buf
    mov rdx, prompt_len     ; count 
    syscall

    mov qword [buf_idx], 0

.read_loop:
    mov rbx, [buf_idx]

    mov rax, 0              ; sys_read
    mov rdi, 0              ; fd = stdin
    lea rsi, [input_buf + rbx]  ; buf
    mov rdx, 1              ; count = 1
    syscall

    mov rbx, [buf_idx]
    lea rsi, [input_buf + rbx]

    mov al, byte [rsi]
    cmp al, 0x0A
    je .process_cmd

    cmp al, 0x08
    je .handle_backspace

    mov rbx, [buf_idx]
    mov byte [input_buf + rbx], al
    mov byte [input_buf + rbx + 1], 0
    inc qword[buf_idx]

    mov rax, 1              ; sys_write
    mov rdi, 1              ; fd = stdout
    lea rsi, [input_buf + rbx]  ; buf
    mov rdx, 1              ; count = 1
    syscall

    jmp .read_loop

.handle_backspace:
    mov rbx, [buf_idx]
    cmp rbx, 0
    je .read_loop

    dec qword [buf_idx]
    mov rbx, [buf_idx]
    mov byte [input_buf + rbx], 0

    mov rax, 1          ; sys_write
    mov rdi, 1          ; fd = stdout
    mov rsi, bs_char    ; buf
    mov rdx, bs_char_len    ; count
    syscall 

    jmp .read_loop

.process_cmd:
    mov rbx, [buf_idx]
    mov byte [input_buf + rbx], 0
    
    mov rax, 1          ; sys_write
    mov rdi, 1          ; fd = stdout
    mov rsi, newline    ; buf
    mov rdx, newline_len
    syscall

.cmp_cmd_hi:
    mov rdi, cmd_hi
    mov rsi, input_buf
    mov rdx, 2
    call compare_cmd

    test rax, rax
    jne .cmp_cmd_clear

    mov rax, 1              ; sys_write
    mov rdi, 1              ; buf = stdout
    mov rsi, msg_hi         ; buf
    mov rdx, msg_hi_len     ; count
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
    mov rax, 0              ; sys_read
    mov rdi, rbx            ; fd
    lea rsi, [file_content] 
    mov rdx, 2047           ; 1 last byte for \0
    syscall 

    cmp rax, 0
    jle .cat_close 

    lea r8, [file_content]
    add r8, rax
    mov byte[r8], 0

    mov rdx, rax            ; count
    mov rax, 1              ; sys_write
    mov rdi, 1              ; stdout
    lea rsi, [file_content] ; buf
    syscall

    jmp .cat_close
.cat_close:
    mov rax, 11
    mov rdi, rbx
    syscall

    mov rax, 1          ; sys_write
    mov rdi, 1          ; fd = stdout
    mov rsi, newline    ; buf
    mov rdx, newline_len
    syscall 

    jmp _start

.cat_fail:
    mov rax, 1                          ; sys_write
    mov rdi, 2                          ; fd = stderr
    mov rsi, msg_file_not_found         ; buf
    mov rdx, msg_file_not_found_len     ; count
    syscall

    jmp _start

.cmp_cmd_exec:
    mov rdi, cmd_exec
    mov rsi, input_buf
    mov rdx, 5
    call starts_with

    test rax, rax
    jne .unknown_cmd

    lea rdi, [input_buf + 5] ; skip "exec "
    lea rsi, [argv_ptrs]
    call parse_args

    mov rdi, [argv_ptrs] ; arg1: fname
    cmp rdi, 0
    ; *fname == NULL ?
    je .exec_fail

    lea rsi, [argv_ptrs]
    mov rax, 7
    syscall

    test rax, rax
    jns .exec_succ

.exec_fail:
    mov rax, 1                          ; sys_write
    mov rdi, 2                          ; fd = stderr
    mov rsi, msg_file_not_found         ; buf
    mov rdx, msg_file_not_found_len     ; count
    syscall

    jmp _start

.exec_succ:
    mov rdi, rax
    xor rsi, rsi
    mov rax, 9
    syscall

    jmp _start

.unknown_cmd:
    mov rax, 1              ; sys_write
    mov rdi, 2              ; fd = stderr
    mov rsi, msg_unknown    ; buff
    mov rdx, msg_unknown_len    ; count
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

;============================================
; parse_args(char* inp_str, char** argv_buf)
; Converts the input string to tokens 
; separated by \0, and 
; store pointers in argv_buf
;============================================
parse_args: 
    push rbx

    xor rcx, rcx ; argc
.skip_spaces:
    cmp byte [rdi], 0x20
    ; *inp_str != " "?
    jne .check_end
    inc rdi
    jmp .skip_spaces

.check_end:
    cmp byte [rdi], 0
    ; *input_str == NULL ?
    je .done

    ; found start of a token
    mov [rsi + rcx*8], rdi ; save token's addr to argv_buf, will be null-terminated in .end_token
    inc rcx

.scan_token:
    cmp byte [rdi], 0x20
    ; *inp_str == " "?
    je .end_token
    cmp byte [rdi], 0
    ; *inp_str == NULL? 
    je .done
    inc rdi
    jmp .scan_token

.end_token:
    mov byte [rdi], 0
    inc rdi
    jmp .skip_spaces 

.done:
    mov qword [rsi + rcx*8], 0 ; add null at the end to terminate the argv
    pop rbx 
    ret