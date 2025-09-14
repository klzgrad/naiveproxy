;Testname=br978756; Arguments=-Ox -felf64 -obr978756.o; Files=stdout stderr br978756.o
[bits 64]
	MOVNTDQA	xmm1, oword [rsi]
	MOVLPD		xmm2, qword [rdi]
	MOVLPD		xmm2, [rdi]
	MOVLPD		qword [rdi], xmm2
	MOVLPD		[rdi], xmm2
