BITS 64
	cpu latevex

	vpdpwsud xmm1, xmm2, xmm0
	vpdpwsud xmm2, xmm3, [rax]
	vpdpwsud xmm3, xmm4, [rax+0x12]
	vpdpwsud xmm4, xmm5, [rax+rbx*2]

	vpdpwsud ymm1, ymm2, ymm0
	vpdpwsud ymm2, ymm3, [rax]
	vpdpwsud ymm3, ymm14, [rax+0x12]
	vpdpwsud ymm14, ymm5, [rax+rbx*2]

	vpdpwsuds xmm1, xmm2, xmm0
	vpdpwsuds xmm2, xmm3, [rax]
	vpdpwsuds xmm3, xmm14, [rax+0x12]
	vpdpwsuds xmm14, xmm5, [rax+rbx*2]

	vpdpwsuds ymm1, ymm2, ymm0
	vpdpwsuds ymm2, ymm3, [rax]
	vpdpwsuds ymm3, ymm14, [rax+0x12]
	vpdpwsuds ymm14, ymm5, [rax+rbx*2]

	vpdpwusd xmm1, xmm2, xmm0
	vpdpwusd xmm2, xmm3, [rax]
	vpdpwusd xmm3, xmm14, [rax+0x12]
	vpdpwusd xmm14, xmm5, [rax+rbx*2]

	vpdpwusd ymm1, ymm2, ymm0
	vpdpwusd ymm2, ymm3, [rax]
	vpdpwusd ymm3, ymm14, [rax+0x12]
	vpdpwusd ymm14, ymm5, [rax+rbx*2]

	vpdpwusds xmm1, xmm2, xmm0
	vpdpwusds xmm2, xmm3, [rax]
	vpdpwusds xmm3, xmm14, [rax+0x12]
	vpdpwusds xmm14, xmm5, [rax+rbx*2]

	vpdpwusds ymm1, ymm2, ymm0
	vpdpwusds ymm2, ymm3, [rax]
	vpdpwusds ymm3, ymm14, [rax+0x12]
	vpdpwusds ymm14, ymm5, [rax+rbx*2]

	vpdpwuud xmm1, xmm2, xmm0
	vpdpwuud xmm2, xmm3, [rax]
	vpdpwuud xmm3, xmm14, [rax+0x12]
	vpdpwuud xmm14, xmm5, [rax+rbx*2]

	vpdpwuud ymm1, ymm2, ymm0
	vpdpwuud ymm2, ymm3, [rax]
	vpdpwuud ymm3, ymm14, [rax+0x12]
	vpdpwuud ymm14, ymm5, [rax+rbx*2]

	vpdpwuuds xmm1, xmm2, xmm0
	vpdpwuuds xmm2, xmm3, [rax]
	vpdpwuuds xmm3, xmm14, [rax+0x12]
	vpdpwuuds xmm14, xmm5, [rax+rbx*2]

	vpdpwuuds ymm1, ymm2, ymm0
	vpdpwuuds ymm2, ymm3, [rax]
	vpdpwuuds ymm3, ymm14, [rax+0x12]
	vpdpwuuds ymm14, ymm5, [rax+rbx*2]

