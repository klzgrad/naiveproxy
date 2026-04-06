	;; All the flavors of RET
%ifndef ERROR
 %define ERROR 0
%endif


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
	retfd
	retq
	retnq
	retfq
