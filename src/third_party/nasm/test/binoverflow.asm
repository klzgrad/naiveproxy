;;  BR 3392655: relocation overflow during bin format link

	[map all binoverflow.map]

	org 7C00h

%macro br3392655 1
	bits %1

	section .text.%1
start%1:

mov al, var%1
%if %1 == 64
mov sil, var%1
%endif

mov al, foo%1
%if %1 == 64
mov sil, foo%1
%endif

mov al, start%1
%if %1 == 64
mov sil, start%1
%endif

	align 16
var%1:	db 0

	align 256
foo%1:	db 0
%endmacro

	br3392655 16
	br3392655 32
	br3392655 64
