	bits 64

; Broken per BR 3392278
	times 4 paddd xmm8, xmm11

; Broken per BR 3392279
	bswap r12d
	times 4 bswap r12d

; Forward jump
	times 128 jmp there

there:
	nop

; Backwards jump
	times 128 jmp there

	section .bss
	times 0x10 resb 0x20
	resb 1
