BITS 64
	cpu latevex
	vpdpbusd xmm1, xmm2, xmm0
	vpdpbusd xmm2, xmm3, [rax]
	vpdpbusd xmm3, xmm4, [rax+0x12]
	vpdpbusd xmm4, xmm5, [rax+rbx*2]

	vpdpbusd ymm1, ymm2, ymm0
	vpdpbusd ymm2, ymm3, [rax]
	vpdpbusd ymm3, ymm4, [rax+0x12]
	vpdpbusd ymm4, ymm5, [rax+rbx*2]

	vpdpbusds xmm1, xmm2, xmm0
	vpdpbusds xmm2, xmm3, [rax]
	vpdpbusds xmm3, xmm4, [rax+0x12]
	vpdpbusds xmm4, xmm5, [rax+rbx*2]

	vpdpbusds ymm1, ymm2, ymm0
	vpdpbusds ymm2, ymm3, [rax]
	vpdpbusds ymm3, ymm4, [rax+0x12]
	vpdpbusds ymm4, ymm5, [rax+rbx*2]

	vpdpwssd xmm1, xmm2, xmm0
	vpdpwssd xmm2, xmm3, [rax]
	vpdpwssd xmm3, xmm4, [rax+0x12]
	vpdpwssd xmm4, xmm5, [rax+rbx*2]

	vpdpwssd ymm1, ymm2, ymm0
	vpdpwssd ymm2, ymm3, [rax]
	vpdpwssd ymm3, ymm4, [rax+0x12]
	vpdpwssd ymm4, ymm5, [rax+rbx*2]

	vpdpwssds xmm1, xmm2, xmm0
	vpdpwssds xmm2, xmm3, [rax]
	vpdpwssds xmm3, xmm4, [rax+0x12]
	vpdpwssds xmm4, xmm5, [rax+rbx*2]

	vpdpwssds ymm1, ymm2, ymm0
	vpdpwssds ymm2, ymm3, [rax]
	vpdpwssds ymm3, ymm4, [rax+0x12]
	vpdpwssds ymm4, ymm5, [rax+rbx*2]
