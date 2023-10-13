	bits 64

	VGATHERQPS xmm1, [xmm0 + rsi], xmm2   ; OK
	VGATHERQPS xmm1, [ymm0 + rsi], xmm2 ; fail: error: invalid effective address
	VGATHERDPD ymm1, [xmm0 + rsi], ymm2   ; OK
	VGATHERDPD xmm1, [xmm0 + rsi], xmm2   ; OK
	VGATHERQPD xmm1, [xmm0 + rsi], xmm2   ; OK
	VGATHERQPD ymm1, [ymm0 + rsi], ymm2   ; OK
	VPGATHERQD xmm1, [xmm0 + rsi], xmm2   ; OK
	VPGATHERQD xmm1, [ymm0 + rsi], xmm2   ; fail: error: invalid effective address
	VPGATHERDQ ymm1, [xmm0 + rsi], ymm2   ; OK
