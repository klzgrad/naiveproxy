	bits 64

%macro b 1
	%1 ax,16
	%1 eax,16
	%1 rax,16
	%1 word [rdi],16
	%1 dword [rdi],16
	%1 qword [rdi],16
	%1 ax,byte 16
	%1 eax,byte 16
	%1 rax,byte 16
	%1 word [rdi],byte 16
	%1 dword [rdi],byte 16
	%1 qword [rdi],byte 16
%endmacro

	b bt
	b btc
	b btr
	b bts

	imul ax,[rdi],16
	imul ax,word [rdi],16
	imul ax,[rdi],byte 16
	imul ax,word [rdi],byte 16

	imul eax,[rdi],16
	imul eax,dword [rdi],16
	imul eax,[rdi],byte 16
	imul eax,dword [rdi],byte 16

	imul rax,[rdi],16
	imul rax,qword [rdi],16
	imul rax,[rdi],byte 16
	imul rax,qword [rdi],byte 16
