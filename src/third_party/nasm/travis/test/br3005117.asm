%macro B_STRUC 1-*
%push foo
%define %$strucname %1
%%top_%$strucname:
%rep %0 - 1
%rotate 1
resb %{$strucname}%1 - ($ - %%top_%$strucname)
%1:
%endrep
resb %{$strucname}_size - ($ - %%top_%$strucname)
%pop
%endmacro

struc timeval
.tv_sec		resd	1
.tv_usec	resd	1
endstruc

mov	[timeval_struct.tv_sec], eax

section .bss

timeval_struct B_STRUC timeval, .tv_sec, .tv_usec
	timeval_struct_len	equ	$ - timeval_struct
