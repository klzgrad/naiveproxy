[bits 64]
	vmaskmovps	[edi],ymm0,ymm1
	vextractf128	xmm0,ymm1,1
	vextractf128	[edi],ymm1,1
