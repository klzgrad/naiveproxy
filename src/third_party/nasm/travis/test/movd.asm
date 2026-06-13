[BITS 32]
	movd	mm0,eax
	movd	mm0,[eax]
	movd	[eax],mm0
	movd	eax,mm0

	movd	xmm0,eax
	movd	xmm0,[eax]

	movd	[eax],xmm0
	movd	eax,xmm0
