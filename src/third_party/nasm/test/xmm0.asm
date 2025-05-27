; BR 3392275: don't require xmm0 to be explicitly declared when implicit

	bits 32

	blendvpd xmm2,xmm1,xmm0
	blendvpd xmm2,xmm1
	blendvps xmm2,xmm1,xmm0
	blendvps xmm2,xmm1
	pblendvb xmm2,xmm1,xmm0
	pblendvb xmm2,xmm1
	sha256rnds2 xmm2,xmm1,xmm0
	sha256rnds2 xmm2,xmm1
