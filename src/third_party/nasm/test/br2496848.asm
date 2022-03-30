;Testname=unoptimized; Arguments=-O0 -fbin -o br2496848.bin; Files=stdout stderr br2496848.bin
;Testname=optimized;   Arguments=-Ox -fbin -o br2496848.bin; Files=stdout stderr br2496848.bin

bits 64

foo:

default abs

mov al, [qword 0xffffffffffffffff]
mov al, [qword 0x1ffffffffffffffff]

mov cl, [byte 0x12345678]

default rel

mov cl, [foo]
mov cl, [foo + 0x10000000]
mov cl, [foo + 0x100000000]

mov cl, [0x100]
mov cl, [$$ + 0x100]

mov cl, [rax - 1]
mov cl, [rax + 0xffffffff]
mov cl, [rax + 0x1ffffffff]

bits 32
mov cl, [eax - 1]
mov cl, [eax + 0xffffffff]
mov cl, [eax + 0x1ffffffff]
mov cl, [byte eax + 0xffffffff]
mov cl, [byte eax + 0x1ffffffff]
mov cl, [byte eax + 0x1000ffff]

bits 16
mov cl, [di - 1]
mov cl, [di + 0xffff]
mov cl, [di + 0x1ffff]
mov cl, [byte di + 0xffff]
mov cl, [byte di + 0x1ffff]
mov cl, [byte di + 0x10ff]
