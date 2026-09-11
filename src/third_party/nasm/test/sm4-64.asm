;Testname=sm4-64; Arguments=-felf -osm4-64.o -O0; Files=stdout stderr sm4-64.o
BITS 64
	vsm4rnds4 xmm1, xmm2, xmm0
	vsm4rnds4 xmm2, xmm3, [rax]
	vsm4rnds4 xmm3, xmm4, [rax+0x12]
	vsm4rnds4 xmm4, xmm5, [rax+rbx*2]

        vsm4rnds4 ymm1, ymm2, ymm0
	vsm4rnds4 ymm2, ymm3, [rax]
	vsm4rnds4 ymm3, ymm4, [rax+0x12]
	vsm4rnds4 ymm4, ymm5, [rax+rbx*2]

	vsm4key4 xmm1, xmm2, xmm0
	vsm4key4 xmm2, xmm3, [rax]
	vsm4key4 xmm3, xmm4, [rax+0x12]
	vsm4key4 xmm4, xmm5, [rax+rbx*2]

        vsm4key4 ymm1, ymm2, ymm0
	vsm4key4 ymm2, ymm3, [rax]
	vsm4key4 ymm3, ymm4, [rax+0x12]
	vsm4key4 ymm4, ymm5, [rax+rbx*2]
