	bits 64
	movhpd xmm0,[rdi+2]
	movhpd xmm0,qword [rdi+2]

	movhpd [rsi+3],xmm1
	movhpd qword [rsi+3],xmm1

	vmovhpd xmm2,xmm1,[rax+4]
	vmovhpd xmm2,xmm1,qword [rax+4]

	vmovhpd xmm3,[rax+4]
	vmovhpd xmm3,qword [rax+4]

	vmovhpd [rcx+5],xmm4
	vmovhpd qword [rcx+5],xmm4
