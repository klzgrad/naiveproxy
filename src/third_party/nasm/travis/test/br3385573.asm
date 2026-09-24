[bits 64]

	vpmovsxbw	ymm1, xmm2
	vpsllw		ymm1, ymm2, 3
	vpslld		ymm1, ymm2, 3
	vpsllq		ymm1, ymm2, 3
	vpsrld		ymm1, ymm2, 3
	vpsrad		ymm1, ymm2, 3
	vpermq		ymm1, [rsi], 9
