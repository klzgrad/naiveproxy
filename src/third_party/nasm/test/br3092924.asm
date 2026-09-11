%define RNUM	0x10000  ; max of relocations in a section is 0xffff

section	.data1
	r1 dd RNUM

section	.data2
	r2 dd RNUM

%macro x1 1
	mov eax, [r1 + %1]
%endmacro

%macro x2 1
	mov eax, [r2 + %1]
%endmacro

section	.text1

	%assign	i 0
	%rep RNUM
		x1 i
		x2 i
		%assign i i+1
	%endrep

