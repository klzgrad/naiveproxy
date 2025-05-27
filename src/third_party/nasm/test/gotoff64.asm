;Testname=noerr; Arguments=-felf64 -ogotoff64.o; Files=stdout stderr gotoff64.o
;Testname=err; Arguments=-DERROR -felf64 -ogotoff64.o; Files=stdout stderr gotoff64.o

	bits 64
	default rel

	extern foo

	mov r15,[foo wrt ..got]
	lea r12,[foo wrt ..got]
%ifdef ERROR
	lea rax,[foo wrt ..gotoff]
	mov rax,[foo wrt ..gotoff]
%endif

	default abs

	mov r15,[foo wrt ..got]
	lea r12,[foo wrt ..got]
	mov rax,[qword foo wrt ..got]
%ifdef ERROR
	lea rax,[foo wrt ..gotoff]
	mov rax,[foo wrt ..gotoff]
%endif
	mov rax,[qword foo wrt ..gotoff]
