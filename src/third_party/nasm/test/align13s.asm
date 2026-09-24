;Testname=unoptimized; Arguments=-O0 -fbin -oalign13s.bin; Files=stdout stderr align13s.bin
;Testname=optimized; Arguments=-Ox -fbin -oalign13s.bin; Files=stdout stderr align13s.bin

; Test of non-power-of-2 alignment

%use smartalign

	bits 32

	inc eax
	inc eax
	align 13
	inc eax
	inc eax
	align 13
	inc eax
	inc eax
	align 13
	inc eax
	inc eax
