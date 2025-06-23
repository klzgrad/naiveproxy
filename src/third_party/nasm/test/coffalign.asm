	section .text align=64
foo:
	nop
	nop
	nop
	ret

	section .data align=64
bar:
	db 0, 1, 2

	section .text align=32
baz:
	nop
	nop
	nop
	ret
