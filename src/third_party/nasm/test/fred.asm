	bits 64
	default rel

	section .text

erets:
	erets
eretu:
	eretu

%macro lgs 1
	mov gs,%1
	lkgs %1
%endmacro

lkgs:
	lgs [foo]
	lgs ax
	lgs word [foo]
	lgs eax
%ifdef ERROR
	lgs dword [foo]
%endif
	lgs rax
%ifdef ERROR
	lgs qword [foo]
%endif

	align 8

	section .data
	alignb 8
foo:
	dq 0
