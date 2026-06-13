BITS 64
	sha1rnds4 xmm1, xmm2, 9
	sha1rnds4 xmm2, [rax], 7
	sha1rnds4 xmm3, [rax+0x12], 5
	sha1rnds4 xmm4, [rax+rbx*2], 1
	sha1nexte xmm1, xmm2
	sha1nexte xmm2, [rax]
	sha1nexte xmm3, [rax+0x12]
	sha1nexte xmm4, [rax+rbx*2]
	sha1msg1 xmm1, xmm2
	sha1msg1 xmm2, [rax]
	sha1msg1 xmm3, [rax+0x12]
	sha1msg1 xmm4, [rax+rbx*2]
	sha1msg2 xmm1, xmm2
	sha1msg2 xmm2, [rax]
	sha1msg2 xmm3, [rax+0x12]
	sha1msg2 xmm4, [rax+rbx*2]
	sha256rnds2 xmm1, xmm2, xmm0
	sha256rnds2 xmm2, [rax], xmm0
	sha256rnds2 xmm3, [rax+0x12], xmm0
	sha256rnds2 xmm4, [rax+rbx*2], xmm0
	sha256msg1 xmm1, xmm2
	sha256msg1 xmm2, [rax]
	sha256msg1 xmm3, [rax+0x12]
	sha256msg1 xmm4, [rax+rbx*2]
	sha256msg2 xmm1, xmm2
	sha256msg2 xmm2, [rax]
	sha256msg2 xmm3, [rax+0x12]
	sha256msg2 xmm4, [rax+rbx*2]
