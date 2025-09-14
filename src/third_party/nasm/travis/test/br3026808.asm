%imacro proc 1
	%push proc
	%assign %$arg 1
%endmacro

%imacro arg 0-1 1
	%assign %$arg %1+%$arg
%endmacro

%imacro endproc 0
	%pop
%endmacro

;----------------------------

proc Test
	%$ARG	arg
endproc
