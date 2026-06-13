;;
;; Test of context-local labels
;;

	bits 64
	extern everywhere	; Test of extern -> global promotion, too
	extern tjosan
here:
	jz .there
%push foo
	jo %$mordor
	hlt
%$mordor:
	nop
%pop
.there:
	ret

everywhere:
	ret

	global everywhere

tjosan:
	ret
