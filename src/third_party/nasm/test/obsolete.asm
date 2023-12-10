	bits 16

	cpu 8086
	pop cs
	mov cs,ax

	cpu 386
	pop cs
	mov cs,ax

	cpu any
	pcommit
