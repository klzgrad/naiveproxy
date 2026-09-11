;Testname=unoptimized; Arguments=-O0 -fbin -obr3174983.bin; Files=stdout stderr br3174983.bin
;Testname=optimized;   Arguments=-Ox -fbin -obr3174983.bin; Files=stdout stderr br3174983.bin

	bits 32
	vpextrw ecx,xmm0,8	; c5 f9 c5 c8 08
	vpextrw ecx,xmm2,3	; c5 f9 c5 ca 03

	bits 64
	vpextrw rcx,xmm0,8	; c5 f9 c5 c8 08
