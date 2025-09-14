;Testname=test; Arguments=-fbin -opushseg.bin; Files=stdout stderr pushseg.bin

	bits 16

	push cs
	push ds
	push es
	push ss
	push fs
	push gs

	pop gs
	pop fs
	pop ss
	pop es
	pop ds
	pop cs		; 8086 only, does not disassemble
