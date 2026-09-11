BITS 32
	cpu latevex

	vpdpwsud xmm1, xmm2, xmm0
	vpdpwsud xmm2, xmm3, [eax]
	vpdpwsud xmm3, xmm4, [eax+0x12]
	vpdpwsud xmm4, xmm5, [eax+ebx*2]

	vpdpwsud ymm1, ymm2, ymm0
	vpdpwsud ymm2, ymm3, [eax]
	vpdpwsud ymm3, ymm4, [eax+0x12]
	vpdpwsud ymm4, ymm5, [eax+ebx*2]

	vpdpwsuds xmm1, xmm2, xmm0
	vpdpwsuds xmm2, xmm3, [eax]
	vpdpwsuds xmm3, xmm4, [eax+0x12]
	vpdpwsuds xmm4, xmm5, [eax+ebx*2]

	vpdpwsuds ymm1, ymm2, ymm0
	vpdpwsuds ymm2, ymm3, [eax]
	vpdpwsuds ymm3, ymm4, [eax+0x12]
	vpdpwsuds ymm4, ymm5, [eax+ebx*2]

	vpdpwusd xmm1, xmm2, xmm0
	vpdpwusd xmm2, xmm3, [eax]
	vpdpwusd xmm3, xmm4, [eax+0x12]
	vpdpwusd xmm4, xmm5, [eax+ebx*2]

	vpdpwusd ymm1, ymm2, ymm0
	vpdpwusd ymm2, ymm3, [eax]
	vpdpwusd ymm3, ymm4, [eax+0x12]
	vpdpwusd ymm4, ymm5, [eax+ebx*2]

	vpdpwusds xmm1, xmm2, xmm0
	vpdpwusds xmm2, xmm3, [eax]
	vpdpwusds xmm3, xmm4, [eax+0x12]
	vpdpwusds xmm4, xmm5, [eax+ebx*2]

	vpdpwusds ymm1, ymm2, ymm0
	vpdpwusds ymm2, ymm3, [eax]
	vpdpwusds ymm3, ymm4, [eax+0x12]
	vpdpwusds ymm4, ymm5, [eax+ebx*2]

	vpdpwuud xmm1, xmm2, xmm0
	vpdpwuud xmm2, xmm3, [eax]
	vpdpwuud xmm3, xmm4, [eax+0x12]
	vpdpwuud xmm4, xmm5, [eax+ebx*2]

	vpdpwuud ymm1, ymm2, ymm0
	vpdpwuud ymm2, ymm3, [eax]
	vpdpwuud ymm3, ymm4, [eax+0x12]
	vpdpwuud ymm4, ymm5, [eax+ebx*2]

	vpdpwuuds xmm1, xmm2, xmm0
	vpdpwuuds xmm2, xmm3, [eax]
	vpdpwuuds xmm3, xmm4, [eax+0x12]
	vpdpwuuds xmm4, xmm5, [eax+ebx*2]

	vpdpwuuds ymm1, ymm2, ymm0
	vpdpwuuds ymm2, ymm3, [eax]
	vpdpwuuds ymm3, ymm4, [eax+0x12]
	vpdpwuuds ymm4, ymm5, [eax+ebx*2]

