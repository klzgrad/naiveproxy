	bits 64

	nop
	o64 nop
	pause
	o64 pause

	xchg ax,ax
	xchg eax,eax
	xchg rax,rax

	rep xchg ax,ax
	rep xchg eax,eax
	rep xchg rax,rax
