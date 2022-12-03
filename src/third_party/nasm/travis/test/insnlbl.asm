;
; Test "instruction as label" -- make opcodes legal as labels if
; they are followed by a colon.
;

do:	jmp dq+2
	dw do, add, sub, dq
add:	jmp add-2
sub:	jmp do+2
dq:	dw $-sub
