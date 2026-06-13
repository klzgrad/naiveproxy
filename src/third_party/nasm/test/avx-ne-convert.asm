BITS 32
	vbcstnebf16ps xmm1, [eax]
	vbcstnebf16ps ymm1, [eax]
	vbcstnebf162ps xmm1, [eax]
	vbcstnebf162ps ymm1, [eax]
	vbcstnesh2ps xmm1, [eax]
	vbcstnesh2ps ymm1, [eax]
	vcvtneebf162ps xmm1, oword [ebx]
	vcvtneebf162ps ymm1, yword [ecx]
	vcvtneeph2ps xmm1, oword [ebx]
	vcvtneeph2ps ymm1, yword [ecx]
	vcvtneobf162ps xmm1, oword [ebx]
	vcvtneobf162ps ymm1, yword [ecx]
	vcvtneoph2ps xmm1, oword [ebx]
	vcvtneoph2ps ymm1, yword [ecx]
cpu latevex
	vcvtneps2bf16 xmm1, xmm2
	vcvtneps2bf16 xmm1, ymm2
	vcvtneps2bf16 xmm1, oword [ebx]
	vcvtneps2bf16 xmm1, yword [ebx]
