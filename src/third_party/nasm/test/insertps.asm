	bits 64
	insertps xmm0,xmm1,16
	insertps xmm0,dword xmm1,16
	insertps xmm0,xmm1,byte 16
	insertps xmm0,dword xmm1,byte 16

	insertps xmm0,[rax],16
	insertps xmm0,dword [rax],16
	insertps xmm0,[rax],byte 16
	insertps xmm0,dword [rax],byte 16
