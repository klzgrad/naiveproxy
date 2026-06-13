%macro bug 1-*
	%push foo
		%define %$v %1
		%define vv %$v_ %+ %1
		%%top_{%$v}%1:
		mov eax, eax
		mov eax, %%top_{%$v}%1
		mov eax, vv
	%pop
%endmacro

bug a
