%macro test 1-3 5 -2
	bits %1

%undef MEM
%if %1 == 16
  %define MEM [di]
%elif %1 == 32
  %define MEM [edi]
%elif %1 == 64
  %define MEM [rdi]
%endif

	imul al
	imul byte MEM
	imul ax
	imul word MEM
	imul eax
	imul dword MEM
%if %1 == 64
	imul rdx
	imul qword MEM
%endif

	imul ax,cx
	imul ax,MEM
	imul ax,word MEM
	imul eax,ecx
	imul eax,MEM
	imul eax,dword MEM
%if %1 == 64
	imul rax,rcx
	imul rax,MEM
	imul rax,qword MEM
%endif

	imul ax,cx,%2
	imul ax,cx,byte %2
	imul ax,MEM,%2
	imul ax,word MEM,%2
	imul eax,ecx,%2
	imul eax,ecx,byte %2
	imul eax,MEM,%2
	imul eax,dword MEM,%2
%if %1 == 64
	imul rax,rcx,%2
	imul rax,rcx,byte %2
	imul rax,MEM,%2
	imul rax,qword MEM,%2
%endif

	imul ax,%2
	imul ax,byte %2
	imul eax,%2
	imul eax,byte %2
%if %1 == 64
	imul rax,%2
	imul rax,byte %2
%endif

	imul ax,cx,0x1234
	imul ax,MEM,0x1234
	imul ax,word MEM,0x1234
	imul eax,ecx,0x12345678
	imul eax,MEM,0x12345678
	imul eax,dword MEM,0x12345678
%if %1 == 64
	imul rax,rcx,0x12345678
	imul rax,MEM,0x12345678
	imul rax,qword MEM,0x12345678
%endif

	imul ax,0x1234
	imul eax,0x12345678
%if %1 == 64
	imul rax,0x12345678
%endif

	imul ax,cx,0xfffe
	imul ax,MEM,0xfffe
	imul ax,word MEM,0xfffe
	imul ax,cx,0xfe
	imul ax,MEM,0xfe
	imul ax,word MEM,0xfe
	imul eax,ecx,0xfffffffe
	imul eax,MEM,0xfffffffe
	imul eax,dword MEM,0xfffffffe
	imul eax,ecx,0xfffe
	imul eax,MEM,0xfffe
	imul eax,dword MEM,0xfffe
%if %1 == 64
	imul rax,rcx,%3
	imul rax,MEM,%3
	imul rax,qword MEM,%3
	imul rax,rcx,0xfffe
	imul rax,MEM,0xfffe
	imul rax,qword MEM,0xfffe
%endif

	imul ax,0xfffe
	imul eax,0xfffffffe
%if %1 == 64
	imul rax,%3
%endif
%endmacro

	test 16
	test 32
	test 64

%ifdef WARN
	test 16,0x999
	test 32,0x999999
	test 64,0x999999999,0xfffffffe
%endif
