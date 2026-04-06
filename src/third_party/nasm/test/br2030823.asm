;Testname=optimized; Arguments=-Ox -fbin -obr2030823.bin; Files=stdout stderr br2030823.bin

bits 64
	VFMADDPD xmm0, xmm1, [0], xmm3
	VFMADDPD xmm0, xmm1, xmm2, [0]
	VFMADDPD ymm0, ymm1, [0], ymm3
	VFMADDPD ymm0, ymm1, ymm2, [0]
