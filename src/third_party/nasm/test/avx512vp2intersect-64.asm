BITS 64
	vp2intersectd k0, xmm1, xmm2
	vp2intersectd k0, ymm1, ymm2
	vp2intersectd k0, zmm1, zmm2

	vp2intersectq k0, xmm1, xmm2
	vp2intersectq k0, ymm1, ymm2
	vp2intersectq k0, zmm1, zmm2

	vp2intersectd k1, xmm1, xmm2
	vp2intersectd k1, ymm1, ymm2
	vp2intersectd k1, zmm1, zmm2

	vp2intersectq k1, xmm1, xmm2
	vp2intersectq k1, ymm1, ymm2
	vp2intersectq k1, zmm1, zmm2

	vp2intersectd k0, xmm1, [rax]
	vp2intersectd k0, ymm1, [rcx+1]
	vp2intersectd k0, zmm1, [2*rdx+64]

	vp2intersectq k0, xmm1, [rax]
	vp2intersectq k0, ymm1, [rcx+1]
	vp2intersectq k0, zmm1, [2*rdx+64]

	vp2intersectd k1, xmm1, [rax]
	vp2intersectd k1, ymm1, [rcx+1]
	vp2intersectd k1, zmm1, [2*rdx+64]

	vp2intersectq k1, xmm1, [rax]
	vp2intersectq k1, ymm1, [rcx+1]
	vp2intersectq k1, zmm1, [2*rdx+64]

	vp2intersectd k0, xmm1, [rax]{1to4}
	vp2intersectd k0, ymm1, [rcx+1]{1to8}
	vp2intersectd k0, zmm1, [2*rdx+4]{1to16}

	vp2intersectq k0, xmm1, [rax]{1to2}
	vp2intersectq k0, ymm1, [rcx+1]{1to4}
	vp2intersectq k0, zmm1, [2*rdx+8]{1to8}

	vp2intersectd k1, xmm1, [rax]{1to4}
	vp2intersectd k1, ymm1, [rcx+1]{1to8}
	vp2intersectd k1, zmm1, [2*rdx+4]{1to16}

	vp2intersectq k1, xmm1, [rax]{1to2}
	vp2intersectq k1, ymm1, [rcx+1]{1to4}
	vp2intersectq k1, zmm1, [2*rdx+8]{1to8}
