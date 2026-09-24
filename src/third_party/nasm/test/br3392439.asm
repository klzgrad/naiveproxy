segment _TEXT class=CODE USE32 align=1 CPU=686

extern _entry

start:
    mov ax, 0x18
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov ax, 0x1234
    shl eax, 4
    add eax, 0x3000
    mov esp, [eax]

    call _entry

.infloop:
    hlt
    jmp .infloop


global _ret_16
_ret_16:
	jmp dword 0x10:0x8000
