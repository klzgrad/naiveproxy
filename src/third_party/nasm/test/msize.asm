	bits 64
	vcvtsi2sd       xmm9,xmm10,ecx
	vcvtsi2sd       xmm9,xmm10,rcx
	vcvtsi2sd       xmm9,xmm10,dword [rdi]
	vcvtsi2sd       xmm9,xmm10,qword [rdi]
