section .text
global gdt_load_tss

;========================
; gdt_load_tss
; Inputs:
;   - rdi: TSS Selector
;========================
gdt_load_tss:
    ltr di
    ret