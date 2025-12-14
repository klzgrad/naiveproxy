;Testname=unoptimized; Arguments=-O0 -fbin -obr3189064.bin; Files=stdout stderr br3189064.bin
;Testname=optimized;   Arguments=-Ox -fbin -obr3189064.bin; Files=stdout stderr br3189064.bin

[bits 64]
	VMASKMOVPS	[edi],ymm0,ymm1
	VEXTRACTF128	xmm0,ymm1,1
	VEXTRACTF128	[edi],ymm1,1
