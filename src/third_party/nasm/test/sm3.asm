;Testname=sm3; Arguments=-felf -osm3.o -O0; Files=stdout stderr sm3.o
BITS 32
	vsm3rnds2 xmm1, xmm2, xmm0, 0
	vsm3msg1 xmm1, xmm2, xmm3
	vsm3msg2 xmm1, xmm2, xmm3
