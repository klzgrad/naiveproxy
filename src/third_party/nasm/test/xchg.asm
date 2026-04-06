;Testname=unoptimized; Arguments=-fbin -oxchg.bin -O0; Files=stdout stderr xchg.bin
;Testname=optimized;   Arguments=-fbin -oxchg.bin -Ox; Files=stdout stderr xchg.bin

%macro x 2
	xchg %1,%2
	xchg %2,%1
%endmacro

	bits 16
	
	x ax,ax
	x ax,cx
	x ax,dx
	x ax,bx
	x ax,sp
	x ax,bp
	x ax,si
	x ax,di
	x eax,eax
	x eax,ecx
	x eax,edx
	x eax,ebx
	x eax,esp
	x eax,ebp
	x eax,esi
	x eax,edi

	bits 32
	
	x ax,ax
	x ax,cx
	x ax,dx
	x ax,bx
	x ax,sp
	x ax,bp
	x ax,si
	x ax,di
	x eax,eax
	x eax,ecx
	x eax,edx
	x eax,ebx
	x eax,esp
	x eax,ebp
	x eax,esi
	x eax,edi

	bits 64
	
	x ax,ax
	x ax,cx
	x ax,dx
	x ax,bx
	x ax,sp
	x ax,bp
	x ax,si
	x ax,di
	x ax,r8w
	x ax,r9w
	x ax,r10w
	x ax,r11w
	x ax,r12w
	x ax,r13w
	x ax,r14w
	x ax,r15w
	x eax,eax
	x eax,ecx
	x eax,edx
	x eax,ebx
	x eax,esp
	x eax,ebp
	x eax,esi
	x eax,edi
	x eax,r8d
	x eax,r9d
	x eax,r10d
	x eax,r11d
	x eax,r12d
	x eax,r13d
	x eax,r14d
	x eax,r15d
	x rax,rax
	x rax,rcx
	x rax,rdx
	x rax,rbx
	x rax,rsp
	x rax,rbp
	x rax,rsi
	x rax,rdi
	x rax,r8
	x rax,r9
	x rax,r10
	x rax,r11
	x rax,r12
	x rax,r13
	x rax,r14
	x rax,r15
