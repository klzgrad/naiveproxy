	bits 64

	movd r8d, mm1
	movd r8, mm1
	movq r8, mm1

	movd [rax], mm1
	movq [rax], mm1
	movd dword [rax], mm1
;	movq dword [rax], mm1
	movd qword [rax], mm1
	movq qword [rax], mm1
	
;	movd mm2, mm1
	movq mm2, mm1
