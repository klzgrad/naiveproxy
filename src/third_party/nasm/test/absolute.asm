;Testname=bin; Arguments=-fbin -oabsolute.bin; Files=stdout stderr absolute.bin
%ifmacro org
	org 7c00h
%endif
init_foo:
	jmp init_bar
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop

init_bar:
	mov [b1],dl
	mov [b2],edx
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	ret

	absolute init_bar+7
b1:	resb 1
b2:	resd 6
