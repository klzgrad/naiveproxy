_start:
	movd ecx,xmm0
	movd [foo],xmm0
	movd dword [foo],xmm0

	movdqa xmm1,xmm0
	movdqa [foo],xmm0
	movdqa oword [foo],xmm0

	cmppd xmm2,xmm3,8
	cmppd xmm2,xmm3,byte 8

	section .bss
foo:	reso 1
