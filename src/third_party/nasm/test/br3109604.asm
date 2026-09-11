;Testname=unoptimized; Arguments=-O0 -fbin -obr3109604.bin; Files=stdout stderr br3109604.bin
;Testname=optimized;   Arguments=-Ox -fbin -obr3109604.bin; Files=stdout stderr br3109604.bin

	bits 64
b0:	vmovd xmm2, [rdx+r9]
e0:	
	
	section .data
len:	dd e0 - b0		; Should be 6
