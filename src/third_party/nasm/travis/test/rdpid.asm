%ifdef ERROR
  %define ERR(x) x
%else
  %define ERR(x)
%endif

	bits 16

	rdpid eax
	ERR(rdpid ax)

	bits 32

	rdpid ebx
	ERR(rdpid bx)

	bits 64

	rdpid rcx
	rdpid ecx
	ERR(rdpid cx)
