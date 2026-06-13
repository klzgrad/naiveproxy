BITS 64

	bndmk bnd1, [r11]
	bndmk bnd1, [rax]
	bndmk bnd1, [0x399]
	bndmk bnd1, [r9+0x3]
	bndmk bnd1, [rax+0x3]
	bndmk bnd1, [3,1*r12]
	bndmk bnd1, [rax+rcx]
	bndmk bnd1, [r11+1*rax+0x3]
	bndmk bnd1, [rbx+1*r9+0x3]

	; bndmov
	bndmov bnd1, [r11]
	bndmov bnd1, [rax]
	bndmov bnd1, [0x399]
	bndmov bnd2, [r9+0x3]
	bndmov bnd2, [rax+0x3]
	bndmov bnd0, [1*r12+0x3]
	bndmov bnd2, [rax+rdx]
	bndmov bnd1, [r11+1*rax+0x3]
	bndmov bnd1, [rbx+1*r9+0x3]
	bndmov bnd0, bnd2

	bndmov [r11], bnd1
	bndmov [rax], bnd1
	bndmov [0x399], bnd1
	bndmov [r9+0x3], bnd2
	bndmov [rax+0x3], bnd2
	bndmov [1*r12+0x3], bnd0
	bndmov [rax+rdx], bnd2
	bndmov [r11+1*rax+0x3], bnd1
	bndmov [rbx+1*r9+0x3], bnd1
	bndmov bnd2, bnd0

	; bndcl
	bndcl bnd1, [r11]
	bndcl bnd1, [rax]
	bndcl bnd1, r11
	bndcl bnd1, rcx
	bndcl bnd1, [0x399]
	bndcl bnd1, [r9+0x3]
	bndcl bnd1, [rax+0x3]
	bndcl bnd1, [1*r12+0x3]
	bndcl bnd1, [rax+rcx]
	bndcl bnd1, [r11+1*rax+0x3]
	bndcl bnd1, [rbx+1*r9+0x3]

	; bndcu
	bndcu bnd1, [r11]
	bndcu bnd1, [rax]
	bndcu bnd1, r11
	bndcu bnd1, rcx
	bndcu bnd1, [0x399]
	bndcu bnd1, [r9+0x3]
	bndcu bnd1, [rax+0x3]
	bndcu bnd1, [1*r12+0x3]
	bndcu bnd1, [rax+rcx]
	bndcu bnd1, [r11+1*rax+0x3]
	bndcu bnd1, [rbx+1*r9+0x3]

	; bndcn
	bndcn bnd1, [r11]
	bndcn bnd1, [rax]
	bndcn bnd1, r11
	bndcn bnd1, rcx
	bndcn bnd1, [0x399]
	bndcn bnd1, [r9+0x3]
	bndcn bnd1, [rax+0x3]
	bndcn bnd1, [1*r9+0x3]
	bndcn bnd1, [rax+rcx]
	bndcn bnd1, [r11+1*rax+0x3]
	bndcn bnd1, [rbx+1*r9+0x3]

	; bndstx
	; next 5 lines should be parsed same
	bndstx [rax+0x3,rbx], bnd0	; NASM - split EA
	bndstx [rax+rbx*1+0x3], bnd0	; GAS
	bndstx [rax+rbx+3], bnd0	; GAS
	bndstx [rax+0x3], bnd0, rbx	; ICC-1
	bndstx [rax+0x3], rbx, bnd0	; ICC-2

	; next 5 lines should be parsed same
	bndstx [,rcx*1], bnd2		; NASM
	bndstx [0,rcx*1], bnd2		; NASM
	bndstx [0], bnd2, rcx		; ICC-1
	bndstx [0], rcx, bnd2		; ICC-2
	bndstx [rcx*1], bnd2		; GAS - rcx is encoded as index only when it is mib

	; next 3 lines should be parsed same
	bndstx [3,1*r12], bnd2		; NASM
	bndstx [1*r12+3], bnd2		; GAS
	bndstx [3], r12, bnd2		; ICC

	bndstx [r12+0x399], bnd3
	bndstx [r11+0x1234], bnd1
	bndstx [rbx+0x1234], bnd2
	bndstx [rdx], bnd1

	; bndldx
	bndldx bnd0, [rax+rbx*1+0x3]
	bndldx bnd2, [rbx+rdx+3]
	bndldx bnd3, [r12+0x399]
	bndldx bnd1, [r11+0x1234]
	bndldx bnd2, [rbx+0x1234]
	bndldx bnd2, [1*rbx+3]
	bndldx bnd2, [1*r12+3]
	bndldx bnd1, [rdx]

	; bnd
	bnd ret
	bnd call      foo
	bnd jmp       foo	; when it becomes a Jb form - short jmp (eb),
				; bnd prefix is silently dropped
	bnd jmp       near 0	; near jmp (opcode e9)
;	bnd jmp       short 0	; explicit short jmp (opcode eb) : error
	bnd jno       foo

foo:	bnd ret
