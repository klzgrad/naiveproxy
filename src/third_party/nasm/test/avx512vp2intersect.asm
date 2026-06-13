BITS 32
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

	vp2intersectd k0, xmm1, [eax]
	vp2intersectd k0, ymm1, [ecx+1]
	vp2intersectd k0, zmm1, [2*edx+64]

	vp2intersectq k0, xmm1, [eax]
	vp2intersectq k0, ymm1, [ecx+1]
	vp2intersectq k0, zmm1, [2*edx+64]

	vp2intersectd k1, xmm1, [eax]
	vp2intersectd k1, ymm1, [ecx+1]
	vp2intersectd k1, zmm1, [2*edx+64]

	vp2intersectq k1, xmm1, [eax]
	vp2intersectq k1, ymm1, [ecx+1]
	vp2intersectq k1, zmm1, [2*edx+64]

	vp2intersectd k0, xmm1, [eax]{1to4}
	vp2intersectd k0, ymm1, [ecx+1]{1to8}
	vp2intersectd k0, zmm1, [2*edx+4]{1to16}

	vp2intersectq k0, xmm1, [eax]{1to2}
	vp2intersectq k0, ymm1, [ecx+1]{1to4}
	vp2intersectq k0, zmm1, [2*edx+8]{1to8}

	vp2intersectd k1, xmm1, [eax]{1to4}
	vp2intersectd k1, ymm1, [ecx+1]{1to8}
	vp2intersectd k1, zmm1, [2*edx+4]{1to16}

	vp2intersectq k1, xmm1, [eax]{1to2}
	vp2intersectq k1, ymm1, [ecx+1]{1to4}
	vp2intersectq k1, zmm1, [2*edx+8]{1to8}
