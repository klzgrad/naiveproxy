	bits 16

	clzero
	clzero ax
	clzero eax
%ifdef ERROR
	clzero rax
%endif

	bits 32

	clzero
	clzero ax
	clzero eax
%ifdef ERROR
	clzero rax
%endif

	bits 64

	clzero
%ifdef ERROR
	clzero ax
%endif
	clzero eax
	clzero rax
