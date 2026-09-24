	bits 64

	movsx ax,al
	movsx eax,al
	movsx eax,ax
	movsx rax,al
	movsx rax,ax
	movsx rax,eax
	movsxd rax,eax

	movsx cx,cl
	movsx ecx,cl
	movsx ecx,cx
	movsx rcx,cl
	movsx rcx,cx
	movsx rcx,ecx
	movsxd rcx,ecx

	movzx ax,al
	movzx eax,al
	movzx eax,ax
	movzx rax,al
	movzx rax,ax
	movzx rax,eax
	movzxd rax,eax

	movzx cx,cl
	movzx ecx,cl
	movzx ecx,cx
	movzx rcx,cl
	movzx rcx,cx
	movzx rcx,ecx
	movzxd rcx,ecx
