	bits 16

	crc32 eax,cl
	crc32 eax,byte [di]
	crc32 eax,cx
	crc32 eax,word [di]
	crc32 eax,ecx
	crc32 eax,dword [di]

	bits 32
	align 16

	crc32 eax,cl
	crc32 eax,byte [edi]
	crc32 eax,cx
	crc32 eax,word [edi]
	crc32 eax,ecx
	crc32 eax,dword [edi]

	bits 64
	align 16

	crc32 eax,cl
	crc32 eax,byte [rdi]
	crc32 eax,r9b
	crc32 eax,cx
	crc32 eax,word [rdi]
	crc32 eax,ecx
	crc32 eax,dword [rdi]
	crc32 rax,cl
	crc32 rax,byte [rdi]
	crc32 rax,r9b
	crc32 rax,rcx
	crc32 rax,qword [rdi]
	crc32 rax,r9
