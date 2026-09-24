BITS 64
	vcvtne2ps2bf16 xmm1, xmm2, xmm3
	vcvtne2ps2bf16 ymm1, ymm2, ymm3
	vcvtne2ps2bf16 zmm1, zmm2, zmm3

	vcvtneps2bf16 xmm1, xmm2
	vcvtneps2bf16 xmm1, ymm2
	vcvtneps2bf16 ymm1, zmm2

	vdpbf16ps xmm1, xmm2, xmm3
	vdpbf16ps ymm1, ymm2, ymm3
	vdpbf16ps zmm1, zmm2, zmm3

	vcvtne2ps2bf16 xmm1, xmm2, [rax]
	vcvtne2ps2bf16 ymm1, ymm2, [rcx+1]
	vcvtne2ps2bf16 zmm1, zmm2, [2*rdx+64]

	vcvtneps2bf16 xmm1, oword [rax]
	vcvtneps2bf16 xmm1, yword [rcx+1]
	vcvtneps2bf16 ymm1, [2*rdx+64]

	vdpbf16ps xmm1, xmm2, [rax]
	vdpbf16ps ymm1, ymm2, [rcx+1]
	vdpbf16ps zmm1, zmm2, [2*rdx+64]

	vcvtne2ps2bf16 xmm1, xmm2, [rax]{1to4}
	vcvtne2ps2bf16 ymm1, ymm2, [rcx+1]{1to8}
	vcvtne2ps2bf16 zmm1, zmm2, [2*rdx+4]{1to16}

	vcvtneps2bf16 xmm1, [rax]{1to4}
	vcvtneps2bf16 xmm1, [rcx+1]{1to8}
	vcvtneps2bf16 ymm1, [2*rdx+4]{1to16}

	vdpbf16ps xmm1, xmm2, [rax]{1to4}
	vdpbf16ps ymm1, ymm2, [rcx+1]{1to8}
	vdpbf16ps zmm1, zmm2, [2*rdx+4]{1to16}

	vcvtne2ps2bf16 xmm1 {k1}, xmm2, xmm3
	vcvtne2ps2bf16 ymm1 {k1}, ymm2, ymm3
	vcvtne2ps2bf16 zmm1 {k1}, zmm2, zmm3

	vcvtneps2bf16 xmm1 {k1}, xmm2
	vcvtneps2bf16 xmm1 {k1}, ymm2
	vcvtneps2bf16 ymm1 {k1}, zmm2

	vdpbf16ps xmm1 {k1}, xmm2, xmm3
	vdpbf16ps ymm1 {k1}, ymm2, ymm3
	vdpbf16ps zmm1 {k1}, zmm2, zmm3

	vcvtne2ps2bf16 xmm1 {k1}, xmm2, [rax]
	vcvtne2ps2bf16 ymm1 {k1}, ymm2, [rcx+1]
	vcvtne2ps2bf16 zmm1 {k1}, zmm2, [2*rdx+64]

	vcvtneps2bf16 xmm1 {k1}, oword [rax]
	vcvtneps2bf16 xmm1 {k1}, yword [rcx+1]
	vcvtneps2bf16 ymm1 {k1}, [2*rdx+64]

	vdpbf16ps xmm1 {k1}, xmm2, [rax]
	vdpbf16ps ymm1 {k1}, ymm2, [rcx+1]
	vdpbf16ps zmm1 {k1}, zmm2, [2*rdx+64]

	vcvtne2ps2bf16 xmm1 {k1}, xmm2, [rax]{1to4}
	vcvtne2ps2bf16 ymm1 {k1}, ymm2, [rcx+1]{1to8}
	vcvtne2ps2bf16 zmm1 {k1}, zmm2, [2*rdx+4]{1to16}

	vcvtneps2bf16 xmm1 {k1}, [rax]{1to4}
	vcvtneps2bf16 xmm1 {k1}, [rcx+1]{1to8}
	vcvtneps2bf16 ymm1 {k1}, [2*rdx+4]{1to16}

	vdpbf16ps xmm1 {k1}, xmm2, [rax]{1to4}
	vdpbf16ps ymm1 {k1}, ymm2, [rcx+1]{1to8}
	vdpbf16ps zmm1 {k1}, zmm2, [2*rdx+4]{1to16}

	vcvtne2ps2bf16 xmm1 {k1}{z}, xmm2, xmm3
	vcvtne2ps2bf16 ymm1 {k1}{z}, ymm2, ymm3
	vcvtne2ps2bf16 zmm1 {k1}{z}, zmm2, zmm3

	vcvtneps2bf16 xmm1 {k1}{z}, xmm2
	vcvtneps2bf16 xmm1 {k1}{z}, ymm2
	vcvtneps2bf16 ymm1 {k1}{z}, zmm2

	vdpbf16ps xmm1 {k1}{z}, xmm2, xmm3
	vdpbf16ps ymm1 {k1}{z}, ymm2, ymm3
	vdpbf16ps zmm1 {k1}{z}, zmm2, zmm3

	vcvtne2ps2bf16 xmm1 {k1}{z}, xmm2, [rax]
	vcvtne2ps2bf16 ymm1 {k1}{z}, ymm2, [rcx+1]
	vcvtne2ps2bf16 zmm1 {k1}{z}, zmm2, [2*rdx+64]

	vcvtneps2bf16 xmm1 {k1}{z}, oword [rax]
	vcvtneps2bf16 xmm1 {k1}{z}, yword [rcx+1]
	vcvtneps2bf16 ymm1 {k1}{z}, [2*rax+64]

	vdpbf16ps xmm1 {k1}{z}, xmm2, [rax]
	vdpbf16ps ymm1 {k1}{z}, ymm2, [rcx+1]
	vdpbf16ps zmm1 {k1}{z}, zmm2, [2*rdx+64]

	vcvtne2ps2bf16 xmm1 {k1}{z}, xmm2, [rax]{1to4}
	vcvtne2ps2bf16 ymm1 {k1}{z}, ymm2, [rcx+1]{1to8}
	vcvtne2ps2bf16 zmm1 {k1}{z}, zmm2, [2*rdx+4]{1to16}

	vcvtneps2bf16 xmm1 {k1}{z}, [rax]{1to4}
	vcvtneps2bf16 xmm1 {k1}{z}, [rcx+1]{1to8}
	vcvtneps2bf16 ymm1 {k1}{z}, [2*rdx+4]{1to16}

	vdpbf16ps xmm1 {k1}{z}, xmm2, [rax]{1to4}
	vdpbf16ps ymm1 {k1}{z}, ymm2, [rcx+1]{1to8}
	vdpbf16ps zmm1 {k1}{z}, zmm2, [2*rdx+4]{1to16}
