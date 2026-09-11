[BITS 64]

	vmovntdqa	ymm1, yword [rsi]	; fails: "error: invalid combination of opcode and operands"
	vmovntdqa	ymm1, [rsi]		; works
	vmovntdqa	xmm1, oword [rsi]	; works
	movntdqa	xmm1, oword [rsi]	; fails, see bug 978756: "error: mismatch in operand sizes"
	movntdqa	xmm1, [rsi]		; works
