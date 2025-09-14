;Testname=sm3-64; Arguments=-felf64 -osm3-64.o -O0; Files=stdout stderr sm3-64.o
BITS 64
	vsm3rnds2 xmm1, xmm2, xmm0, 0
        vsm3msg1 xmm1, xmm2, xmm3
	vsm3msg2 xmm1, xmm2, xmm3
