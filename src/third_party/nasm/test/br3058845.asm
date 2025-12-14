;Testname=unoptimized; Arguments=-O0 -fbin -obr3058845.bin; Files=stdout stderr br3058845.bin
;Testname=optimized;   Arguments=-Ox -fbin -obr3058845.bin; Files=stdout stderr br3058845.bin

BITS 16
cmp ax, 0xFFFF
cmp eax, 0xFFFF_FFFF

BITS 32
cmp ax, 0xFFFF
cmp eax, 0xFFFF_FFFF

BITS 64
cmp ax, 0xFFFF
cmp eax, 0xFFFF_FFFF
