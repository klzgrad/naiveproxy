[bits 64]
	movntdqa	xmm1, oword [rsi]
	movlpd		xmm2, qword [rdi]
	movlpd		xmm2, [rdi]
	movlpd		qword [rdi], xmm2
	movlpd		[rdi], xmm2
