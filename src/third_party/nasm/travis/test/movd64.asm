	bits 64

	movd r8d, mm1
	movd r8, mm1
	movq r8, mm1

	movd [rax], mm1
	movq [rax], mm1
	movd dword [rax], mm1
%ifdef ERROR
	movq dword [rax], mm1
%endif
	movd qword [rax], mm1
	movq qword [rax], mm1

%ifdef ERROR
	movd mm2, mm1
%endif
	movq mm2, mm1
