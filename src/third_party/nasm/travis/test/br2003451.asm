	cpu 8086
	org 0

	; MOV r/m16,imm16
	; (imm16 given as number)
	mov word [bx], 10h

	; MOV r/m16,imm16
	; (imm16 given as label)
	mov word [bx], label

	align 10h

	; This label is at address 10h
label:
