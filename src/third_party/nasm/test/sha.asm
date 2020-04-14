;Testname=sha; Arguments=-felf -osha.o -O0; Files=stdout stderr sha.o
BITS 32

	sha1rnds4 xmm1, xmm2, 9
	sha1rnds4 xmm2, [eax], 7
	sha1rnds4 xmm3, [eax+0x12], 5
	sha1rnds4 xmm4, [eax+ebx*2], 1
	sha1nexte xmm1, xmm2
	sha1nexte xmm2, [eax]
	sha1nexte xmm3, [eax+0x12]
	sha1nexte xmm4, [eax+ebx*2]
	sha1msg1 xmm1, xmm2
	sha1msg1 xmm2, [eax]
	sha1msg1 xmm3, [eax+0x12]
	sha1msg1 xmm4, [eax+ebx*2]
	sha1msg2 xmm1, xmm2
	sha1msg2 xmm2, [eax]
	sha1msg2 xmm3, [eax+0x12]
	sha1msg2 xmm4, [eax+ebx*2]
	sha256rnds2 xmm1, xmm2, xmm0
	sha256rnds2 xmm2, [eax], xmm0
	sha256rnds2 xmm3, [eax+0x12], xmm0
	sha256rnds2 xmm4, [eax+ebx*2], xmm0
	sha256msg1 xmm1, xmm2
	sha256msg1 xmm2, [eax]
	sha256msg1 xmm3, [eax+0x12]
	sha256msg1 xmm4, [eax+ebx*2]
	sha256msg2 xmm1, xmm2
	sha256msg2 xmm2, [eax]
	sha256msg2 xmm3, [eax+0x12]
	sha256msg2 xmm4, [eax+ebx*2]
