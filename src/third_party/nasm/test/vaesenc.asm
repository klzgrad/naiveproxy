;; BR 3392454, 3392460

	bits 64
	aesenc xmm0,xmm4
	vaesenc zmm0,zmm0,zmm4
	vpclmullqlqdq zmm1,zmm1,zmm5
	vpclmulqdq zmm0, zmm1, zmm2, 0
	vaesenclast zmm0, zmm1, zmm2

	bits 32
	aesenc xmm0,xmm4
	vaesenc zmm0,zmm0,zmm4
	vpclmullqlqdq zmm1,zmm1,zmm5
	vpclmulqdq zmm0, zmm1, zmm2, 0
	vaesenclast zmm0, zmm1, zmm2

	bits 16
	aesenc xmm0,xmm4
	vaesenc zmm0,zmm0,zmm4
	vpclmullqlqdq zmm1,zmm1,zmm5
	vpclmulqdq zmm0, zmm1, zmm2, 0
	vaesenclast zmm0, zmm1, zmm2
