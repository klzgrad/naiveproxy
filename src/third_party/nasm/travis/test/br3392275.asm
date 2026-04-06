	bits 32

	blendvpd xmm2,xmm1,xmm0
	blendvpd xmm2,xmm1
	blendvps xmm2,xmm1,xmm0
	blendvps xmm2,xmm1
	pblendvb xmm2,xmm1,xmm0
	pblendvb xmm2,xmm1
	sha256rnds2 xmm2,xmm1,xmm0
	sha256rnds2 xmm2,xmm1
