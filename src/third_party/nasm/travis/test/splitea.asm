	bits 32

	mov eax,[eax]
	mov eax,[eax+ecx]
	mov eax,[eax+ecx*4]
	mov eax,[eax+ecx*4+8]

	mov eax,[eax]
	mov eax,[eax,ecx]
	mov eax,[eax,ecx*4]
	mov eax,[eax+8,ecx*4]
