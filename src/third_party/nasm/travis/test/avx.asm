		bits 64
		blendvpd	xmm2,xmm1,xmm0

		vblendvpd	xmm2,xmm1,xmm0,xmm0
		vblendvpd	xmm2,xmm1,xmm0
		vblendvpd	ymm2,ymm1,ymm0,ymm0
		vblendvpd	ymm2,ymm1,ymm0

		vcvtsi2sd	xmm9,xmm10,ecx
		vcvtsi2sd	xmm9,xmm10,rcx
		vcvtsi2sd	xmm9,xmm10,dword [rdi]
		vcvtsi2sd	xmm9,xmm10,qword [rdi]

		vpextrb		[rax],xmm1,0x33
		vpextrw		[rax],xmm1,0x33
		vpextrd		[rax],xmm1,0x33
		vpextrq		[rax],xmm1,0x33
		vpextrb		rax,xmm1,0x33
		vpextrw		rax,xmm1,0x33
		vpextrd		rax,xmm1,0x33
		vpextrq		rax,xmm1,0x33
		vpextrb		eax,xmm1,0x33
		vpextrw		eax,xmm1,0x33
		vpextrd		eax,xmm1,0x33
;		vpextrq		eax,xmm1,0x33

		pextrw		rax,xmm0,0x33

		vcvtpd2ps	xmm0,xmm1
		vcvtpd2ps	xmm0,oword [rsi]
		vcvtpd2ps	xmm0,ymm1
		vcvtpd2ps	xmm0,yword [rsi]
;		vcvtpd2ps	xmm0,[rsi]

		vcvtpd2dq	xmm0,xmm1
		vcvtpd2dq	xmm0,oword [rsi]
		vcvtpd2dq	xmm0,ymm1
		vcvtpd2dq	xmm0,yword [rsi]
;		vcvtpd2dq	xmm0,[rsi]

		vcvttpd2dq	xmm0,xmm1
		vcvttpd2dq	xmm0,oword [rsi]
		vcvttpd2dq	xmm0,ymm1
		vcvttpd2dq	xmm0,yword [rsi]
;		vcvttpd2dq	xmm0,[rsi]
