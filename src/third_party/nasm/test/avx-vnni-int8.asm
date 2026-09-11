BITS 32
	cpu latevex

	vpdpbsud xmm1, xmm2, xmm0
	vpdpbsud xmm2, xmm3, [eax]
	vpdpbsud xmm3, xmm4, [eax+0x12]
	vpdpbsud xmm4, xmm5, [eax+ebx*2]

	vpdpbsud ymm1, ymm2, ymm0
	vpdpbsud ymm2, ymm3, [eax]
	vpdpbsud ymm3, ymm4, [eax+0x12]
	vpdpbsud ymm4, ymm5, [eax+ebx*2]

	vpdpbsuds xmm1, xmm2, xmm0
	vpdpbsuds xmm2, xmm3, [eax]
	vpdpbsuds xmm3, xmm4, [eax+0x12]
	vpdpbsuds xmm4, xmm5, [eax+ebx*2]

	vpdpbsuds ymm1, ymm2, ymm0
	vpdpbsuds ymm2, ymm3, [eax]
	vpdpbsuds ymm3, ymm4, [eax+0x12]
	vpdpbsuds ymm4, ymm5, [eax+ebx*2]

	vpdpbssd xmm1, xmm2, xmm0
	vpdpbssd xmm2, xmm3, [eax]
	vpdpbssd xmm3, xmm4, [eax+0x12]
	vpdpbssd xmm4, xmm5, [eax+ebx*2]

	vpdpbssd ymm1, ymm2, ymm0
	vpdpbssd ymm2, ymm3, [eax]
	vpdpbssd ymm3, ymm4, [eax+0x12]
	vpdpbssd ymm4, ymm5, [eax+ebx*2]

	vpdpbssds xmm1, xmm2, xmm0
	vpdpbssds xmm2, xmm3, [eax]
	vpdpbssds xmm3, xmm4, [eax+0x12]
	vpdpbssds xmm4, xmm5, [eax+ebx*2]

	vpdpbssds ymm1, ymm2, ymm0
	vpdpbssds ymm2, ymm3, [eax]
	vpdpbssds ymm3, ymm4, [eax+0x12]
	vpdpbssds ymm4, ymm5, [eax+ebx*2]

	vpdpbuud xmm1, xmm2, xmm0
	vpdpbuud xmm2, xmm3, [eax]
	vpdpbuud xmm3, xmm4, [eax+0x12]
	vpdpbuud xmm4, xmm5, [eax+ebx*2]

	vpdpbuud ymm1, ymm2, ymm0
	vpdpbuud ymm2, ymm3, [eax]
	vpdpbuud ymm3, ymm4, [eax+0x12]
	vpdpbuud ymm4, ymm5, [eax+ebx*2]

	vpdpbuuds xmm1, xmm2, xmm0
	vpdpbuuds xmm2, xmm3, [eax]
	vpdpbuuds xmm3, xmm4, [eax+0x12]
	vpdpbuuds xmm4, xmm5, [eax+ebx*2]

	vpdpbuuds ymm1, ymm2, ymm0
	vpdpbuuds ymm2, ymm3, [eax]
	vpdpbuuds ymm3, ymm4, [eax+0x12]
	vpdpbuuds ymm4, ymm5, [eax+ebx*2]

