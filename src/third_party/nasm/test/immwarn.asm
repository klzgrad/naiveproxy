;Testname=onowarn; Arguments=-Ox -DOPT=1 -DWARN=0 -fbin -oimmwarn.bin; Files=stdout stderr immwarn.bin
;Testname=owarn; Arguments=-Ox -DOPT=1 -DWARN=1 -fbin -oimmwarn.bin; Files=stdout stderr immwarn.bin
;Testname=nowarn; Arguments=-O0 -DOPT=0 -DWARN=0 -fbin -oimmwarn.bin; Files=stdout stderr immwarn.bin
;Testname=warn; Arguments=-O0 -DOPT=1 -DWARN=1 -fbin -oimmwarn.bin; Files=stdout stderr immwarn.bin

%ifndef WARN
  %define WARN 1
%endif

	bits 16
	push 1
%if WARN
	push 0ffffffffh
%endif
	push -1
	push 0ffffh
	push byte 0FFFFh

	add ax,0FFFFh
%if WARN
	add ax,0FFFFFFFFh
%endif
	add ax,-1
	add ax,byte 0FFFFh
%if WARN
	add ax,byte 0FFFFFFFFh
%endif
	add ax,-1

	add cx,0FFFFh
%if WARN
	add cx,0FFFFFFFFh
%endif
	add cx,-1
	add cx,byte 0FFFFh
%if WARN
	add cx,byte 0FFFFFFFFh
%endif
	add cx,-1

	bits 32
	push 1
	push 0ffffffffh
	push -1
	push 0ffffh

	push byte 1
%if WARN
	push byte 0ffffh
%endif
	push byte -1

	push word 1
	push word 0ffffh
	push word -1

	push dword 1
	push dword 0ffffffffh
	push dword -1

	add eax,0FFFFh
	add eax,0FFFFFFFFh
	add eax,-1

	add ecx,0FFFFh
	add ecx,0FFFFFFFFh
	add ecx,-1

	bits 64
	mov eax,7fffffffh
	mov eax,80000000h
	mov rax,7fffffffh
	mov rax,80000000h
%if WARN
	mov rax,dword 80000000h
%endif
	add rcx,0FFFFh
%if WARN
	add rcx,0FFFFFFFFh
%endif
	add rcx,-1

	add ecx,0FFFFh
	add ecx,0FFFFFFFFh
	add ecx,-1

	push byte 1
%if WARN
	push byte 0ffffffffh
%endif
	push byte -1
