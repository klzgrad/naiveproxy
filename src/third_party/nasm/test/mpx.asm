;Testname=mpx; Arguments=-felf -ompx.o -O0; Files=stdout stderr mpx.o
BITS 32

	bndmk bnd1, [eax]
	bndmk bnd1, [0x399]
	bndmk bnd1, [ecx+0x3]
	bndmk bnd1, [eax+ecx]
	bndmk bnd1, [ecx*1]
	bndmk bnd1, [edx+1*eax+0x3]

	; bndmov
	bndmov bnd1, [eax]
	bndmov bnd1, [0x399]
	bndmov bnd1, [ecx+0x3]
	bndmov bnd1, [eax+ecx]
	bndmov bnd1, [ecx*1]
	bndmov bnd1, [edx+1*eax+0x3]
	bndmov bnd0, bnd1

	bndmov [eax], bnd1
	bndmov [0x399], bnd1
	bndmov [ecx+0x3], bnd1
	bndmov [eax+ecx], bnd1
	bndmov [ecx*1], bnd1
	bndmov [edx+1*eax+0x3], bnd1
	bndmov bnd1, bnd0

	; bndcl
	bndcl bnd1, [eax]
	bndcl bnd1, ecx
	bndcl bnd1, [0x399]
	bndcl bnd1, [ecx+0x3]
	bndcl bnd1, [eax+ecx]
	bndcl bnd1, [ecx*1]
	bndcl bnd1, [edx+1*eax+0x3]

	; bndcu
	bndcu bnd1, [eax]
	bndcu bnd1, ecx
	bndcu bnd1, [0x399]
	bndcu bnd1, [ecx+0x3]
	bndcu bnd1, [eax+ecx]
	bndcu bnd1, [ecx*1]
	bndcu bnd1, [edx+1*eax+0x3]

	; bndcn
	bndcn bnd1, [eax]
	bndcn bnd1, ecx
	bndcn bnd1, [0x399]
	bndcn bnd1, [ecx+0x3]
	bndcn bnd1, [eax+ecx]
	bndcn bnd1, [ecx*1]
	bndcn bnd1, [edx+1*eax+0x3]

	; bndstx
	bndstx [eax+ebx*1+0x3], bnd0
	bndstx [eax+0x3,ebx], bnd0
	bndstx [eax+0x3], bnd0, ebx
	bndstx [eax+0x3], ebx, bnd0
	bndstx [ecx*1], bnd2
	bndstx [,ecx*1], bnd2
	bndstx [0,ecx*1], bnd2
	bndstx [0], bnd2, ecx
	bndstx [0], ecx, bnd2
	bndstx [edx+0x399], bnd3
	bndstx [1*ebx+3], bnd2
	bndstx [3,1*ebx], bnd2
	bndstx [3], ebx, bnd2
	bndstx [edx], bnd1

	; bndldx
	bndldx bnd0, [eax+ebx*1+0x3]
	bndldx bnd2, [ebx+edx+3]
	bndldx bnd2, [ecx*1]
	bndldx bnd3, [edx+0x399]
	bndldx bnd2, [1*ebx+3]
	bndldx bnd2, [3], ebx
	bndldx bnd1, [edx]

	; bnd
	bnd ret
	bnd call      foo
	bnd jmp       foo	; when it becomes a Jb form - short jmp (eb),
				; bnd prefix is silently dropped
	bnd jmp       near 0	; near jmp (opcode e9)
;	bnd jmp       short 0	; explicit short jmp (opcode eb) : error
	bnd jno       foo

foo:	bnd ret
