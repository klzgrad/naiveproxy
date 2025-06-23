	bits 64
	insertps xmm0,xmm1,16
	insertps xmm0,dword xmm2,16
	insertps xmm0,xmm2,byte 16
	insertps xmm0,dword xmm2,byte 16

	insertps xmm0,[rax],16
	insertps xmm0,dword [rbx],16
	insertps xmm0,[rcx],byte 16
	insertps xmm0,dword [rdx],byte 16
