	bits 32
	movd mm0,eax
	movd mm0,[foo]
	movq mm0,[foo]
	movd mm0,dword [foo]
	movq mm0,qword [foo]
	movmskps eax,xmm1
	movmskpd eax,xmm1
	nop
	movd xmm0,eax
	movd xmm0,[foo]
	movq xmm0,[foo]
	movd xmm0,dword [foo]
	movq xmm0,qword [foo]
	nop

	bits 64
	movd mm0,eax
	movq mm0,[foo]
	movd mm0,dword [foo]
	movq mm0,qword [foo]
	movq mm0,rax
	movmskps eax,xmm1
	movmskpd eax,xmm1
	nop
	movd xmm0,eax
	movq xmm0,[foo]
	movd xmm0,dword [foo]
	movq xmm0,qword [foo]
	movq xmm0,rax
	movmskps rax,xmm1
	movmskpd rax,xmm1
	nop

	section .bss
foo	resq 1
