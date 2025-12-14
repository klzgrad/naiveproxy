	bits 64

	mov eax,1
	mov eax,-1
	mov eax,0x11111111
	mov ecx,2
	add ecx,-6
	add ecx,strict dword -6
	add ecx,4
	add ecx,strict dword 4
	add ecx,10000
	xor ecx,0xffffffff
	xor ecx,dword 0xffffffff
	xor ecx,strict dword 0xffffffff
	xor ecx,-1
	xor ecx,dword -1
	xor ecx,strict dword -1
	add edx,byte ($-$$)
%ifnidn __OUTPUT_FORMAT__,bin
	extern foo, bar
	add eax,byte foo
	add edx,byte (bar-$$)
%endif
