;Testname=O0; Arguments=-O0 -fbin -oaddr64.bin; Files=stdout stderr addr64.bin
;Testname=O1; Arguments=-O1 -fbin -oaddr64.bin; Files=stdout stderr addr64.bin
;Testname=Ox; Arguments=-Ox -fbin -oaddr64.bin; Files=stdout stderr addr64.bin
	bits	64
	mov	rdx,[rax]
	mov	eax,[byte rsp+0x01]
	mov	eax,[byte rsp-0x01]
	mov	eax,[byte rsp+0xFF]
	mov	eax,[byte rsp-0xFF]
	mov	eax,[rsp+0x08]
	mov	eax,[rsp-0x01]
	mov	eax,[rsp+0xFF]
	mov	eax,[rsp-0xFF]
	mov	rax,[rsp+56]
	mov	[rsi],dl
	mov	byte [rsi],'-'
	mov	[rsi],al
	mov	byte [rsi],' '
