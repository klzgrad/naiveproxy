section .text

global _start

_start:
    mov rdi, 0   ; Exit status
    mov rax, 60  ; Exit syscall number
    syscall
