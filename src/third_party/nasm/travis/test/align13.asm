; Test of non-power-of-2 alignment

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
	align 13 ;should do nothing
	inc eax
	inc eax
