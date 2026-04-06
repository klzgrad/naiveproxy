;Testname=br3392259; Arguments=-Ox -felf64 -obr3392259.o; Files=stdout stderr br3392259.o
[BITS 64]

	VMOVNTDQA	ymm1, yword [rsi]	; fails: "error: invalid combination of opcode and operands"
	VMOVNTDQA	ymm1, [rsi]		; works
	VMOVNTDQA	xmm1, oword [rsi]	; works
	MOVNTDQA	xmm1, oword [rsi]	; fails, see bug 978756: "error: mismatch in operand sizes"
	MOVNTDQA	xmm1, [rsi]		; works
