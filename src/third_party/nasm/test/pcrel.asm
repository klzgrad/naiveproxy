	bits 32
foo:				; Backwards reference
	mov eax,[foo - $]
	mov ebx,[ebx + foo - $]
	mov ecx,foo - $
	mov edx,foo - bar

	mov eax,[bar - $]
	mov ebx,[ebx + bar - $]
	mov ecx,bar - $
	mov edx,bar - foo

	mov eax,[baz - $]
	mov ebx,[ebx + baz - $]
	mov esi,[baz - bar]
	mov ecx,baz - $
	mov edx,baz - bar

	bits 64
	default rel

	times 16 nop

	mov eax,[foo]
	mov eax,[foo - $]
	mov eax,[abs foo - $]
	mov ebx,[rbx + foo - $]
	mov ecx,foo - $
	mov edx,foo - bar

	mov eax,[bar]
	mov eax,[bar - $]
	mov eax,[abs bar - $]
	mov ebx,[rbx + bar - $]
	mov ecx,bar - $
	mov edx,bar - foo

	mov eax,[baz]
	mov eax,[baz - $]
	mov eax,[abs baz - $]
	mov ebx,[rbx + baz - $]
	mov esi,[baz - bar]
	mov esi,[abs baz - bar]
	mov ecx,baz - $
	mov edx,baz - bar

bar:				; Forwards reference
	hlt

	section ".data"
baz:				; Other-segment reference
	dd 0
