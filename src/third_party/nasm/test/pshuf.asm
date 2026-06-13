	bits 32
	pextrw eax, mm0, 14
	pextrw eax, mm0, byte 14
	pextrw eax, qword mm0, byte 14

	pinsrw mm1, [ebx], 2
	pinsrw mm1, word [ebx], 2
	pinsrw mm1, word [ebx], byte 2
	pinsrw mm1, edx, 2

	pshufw	mm2, mm3, 4
	pshufw	mm2, mm3, byte 4
	pshufw	mm2, [ebx], 4
	pshufw	mm2, qword [ebx], 4
	pshufw	mm2, [ebx], byte 4
	pshufw	mm2, qword [ebx], byte 4

	pshufd	xmm2, xmm3, 4
	pshufd	xmm2, xmm3, byte 4
	pshufd	xmm2, [ebx], 4
	pshufd	xmm2, qword [ebx], 4
	pshufd	xmm2, [ebx], byte 4
	pshufd	xmm2, qword [ebx], byte 4
