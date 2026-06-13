BITS 64
	vbcstnebf16ps xmm1, [rax]
	vbcstnebf16ps ymm1, [rax]
	vbcstnebf162ps xmm1, [rax]
	vbcstnebf162ps ymm1, [rax]
	vbcstnesh2ps xmm1, [rax]
	vbcstnesh2ps ymm1, [rax]
	vcvtneebf162ps xmm1, oword [rbx]
	vcvtneebf162ps ymm1, yword [rcx]
	vcvtneeph2ps xmm1, oword [rbx]
	vcvtneeph2ps ymm1, yword [rcx]
	vcvtneobf162ps xmm1, oword [rbx]
	vcvtneobf162ps ymm1, yword [rcx]
	vcvtneoph2ps xmm1, oword [rbx]
	vcvtneoph2ps ymm1, yword [rcx]
cpu latevex
	vcvtneps2bf16 xmm1, xmm2
	vcvtneps2bf16 xmm1, ymm2
	vcvtneps2bf16 xmm1, oword [rbx]
	vcvtneps2bf16 xmm1, yword [rbx]
