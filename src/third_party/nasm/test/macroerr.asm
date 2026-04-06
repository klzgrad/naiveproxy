%include "macroerr.inc"

%macro bluttan 1
	mov eax,%1
	blej %1
%endmacro

	bluttan ptr
	blej ptr
	dd ptr, ptr
	
ptr:
