;; All the flavors of RET
%ifndef ERROR
	%define ERROR 0
%endif

%ifdef TEST_BITS_16
	bits 16

	ret
	retn
	retf
	retw
	retnw
	retfw
	retd
	retnd
	retfd
%if ERROR
	retq
	retnq
	retfq
%endif
%endif

%ifdef TEST_BITS_32
	bits 32

	ret
	retn
	retf
	retw
	retnw
	retfw
	retd
	retnd
	retfd
%if ERROR
	retq
	retnq
	retfq
%endif
%endif

%ifdef TEST_BITS_64
	bits 64

	ret
	retn
	retf		; Probably should have been RETFQ, but: legacy...
	retw
	retnw
	retfw
%if ERROR
	retd
	retnd
%endif
%endif
	retfd
	retq
	retnq
	retfq
