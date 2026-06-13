;Testname=sm4; Arguments=-felf -osm4.o -O0; Files=stdout stderr sm4.o
BITS 32
	vsm4rnds4 xmm1, xmm2, xmm0
	vsm4rnds4 xmm2, xmm3, [eax]
	vsm4rnds4 xmm3, xmm4, [eax+0x12]
	vsm4rnds4 xmm4, xmm5, [eax+ebx*2]

        vsm4rnds4 ymm1, ymm2, ymm0
	vsm4rnds4 ymm2, ymm3, [eax]
	vsm4rnds4 ymm3, ymm4, [eax+0x12]
	vsm4rnds4 ymm4, ymm5, [eax+ebx*2]

	vsm4key4 xmm1, xmm2, xmm0
	vsm4key4 xmm2, xmm3, [eax]
	vsm4key4 xmm3, xmm4, [eax+0x12]
	vsm4key4 xmm4, xmm5, [eax+ebx*2]

        vsm4key4 ymm1, ymm2, ymm0
	vsm4key4 ymm2, ymm3, [eax]
	vsm4key4 ymm3, ymm4, [eax+0x12]
	vsm4key4 ymm4, ymm5, [eax+ebx*2]
