BITS 64
	cpu latevex

	vpdpbsud xmm1, xmm2, xmm0
	vpdpbsud xmm2, xmm3, [rax]
	vpdpbsud xmm3, xmm14, oword [rax+0x12]
	vpdpbsud xmm14, xmm5, [rax+rbx*2]

	vpdpbsud ymm1, ymm2, ymm0
	vpdpbsud ymm2, ymm3, [rax]
	vpdpbsud ymm3, ymm14, yword [rax+0x12]
	vpdpbsud ymm14, ymm5, [rax+rbx*2]

	vpdpbsuds xmm1, xmm2, xmm0
	vpdpbsuds xmm2, xmm3, [rax]
	vpdpbsuds xmm3, xmm14, [rax+0x12]
	vpdpbsuds xmm14, xmm5, [rax+rbx*2]

	vpdpbsuds ymm1, ymm2, ymm0
	vpdpbsuds ymm2, ymm3, [rax]
	vpdpbsuds ymm3, ymm14, [rax+0x12]
	vpdpbsuds ymm14, ymm5, [rax+rbx*2]

	vpdpbssd xmm1, xmm2, xmm0
	vpdpbssd xmm2, xmm3, [rax]
	vpdpbssd xmm3, xmm14, [rax+0x12]
	vpdpbssd xmm14, xmm5, [rax+rbx*2]

	vpdpbssd ymm1, ymm2, ymm0
	vpdpbssd ymm2, ymm3, [rax]
	vpdpbssd ymm3, ymm14, [rax+0x12]
	vpdpbssd ymm14, ymm5, [rax+rbx*2]

	vpdpbssds xmm1, xmm2, xmm0
	vpdpbssds xmm2, xmm3, [rax]
	vpdpbssds xmm3, xmm14, [rax+0x12]
	vpdpbssds xmm14, xmm5, [rax+rbx*2]

	vpdpbssds ymm1, ymm2, ymm0
	vpdpbssds ymm2, ymm3, [rax]
	vpdpbssds ymm3, ymm14, [rax+0x12]
	vpdpbssds ymm14, ymm5, [rax+rbx*2]

	vpdpbuud xmm1, xmm2, xmm0
	vpdpbuud xmm2, xmm3, [rax]
	vpdpbuud xmm3, xmm14, [rax+0x12]
	vpdpbuud xmm14, xmm5, [rax+rbx*2]

	vpdpbuud ymm1, ymm2, ymm0
	vpdpbuud ymm2, ymm3, [rax]
	vpdpbuud ymm3, ymm14, [rax+0x12]
	vpdpbuud ymm14, ymm5, [rax+rbx*2]

	vpdpbuuds xmm1, xmm2, xmm0
	vpdpbuuds xmm2, xmm3, [rax]
	vpdpbuuds xmm3, xmm14, [rax+0x12]
	vpdpbuuds xmm14, xmm5, [rax+rbx*2]

	vpdpbuuds ymm1, ymm2, ymm0
	vpdpbuuds ymm2, ymm3, [rax]
	vpdpbuuds ymm3, ymm14, [rax+0x12]
	vpdpbuuds ymm14, ymm5, [rax+rbx*2]


