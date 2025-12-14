;Testname=unoptimized; Arguments=-O0 -fbin -obr3187743.bin; Files=stdout stderr br3187743.bin
;Testname=optimized;   Arguments=-Ox -fbin -obr3187743.bin; Files=stdout stderr br3187743.bin

	bits 64

	vlddqu	xmm0,[edi]
	vlddqu	ymm0,[edi]
