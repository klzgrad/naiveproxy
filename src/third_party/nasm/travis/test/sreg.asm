	bits 64
	mov es,rax
	mov ss,rax
	mov ds,rax
	mov fs,rax
	mov gs,rax
	mov es,eax
	mov ss,eax
	mov ds,eax
	mov fs,eax
	mov gs,eax
	mov es,ax
	mov ss,ax
	mov ds,ax
	mov fs,ax
	mov gs,ax
	mov es,[rsi]
	mov ss,[rsi]
	mov ds,[rsi]
	mov fs,[rsi]
	mov gs,[rsi]
	mov es,word [rsi]
	mov ss,word [rsi]
	mov ds,word [rsi]
	mov fs,word [rsi]
	mov gs,word [rsi]
%ifdef ERR
	mov es,qword [rsi]
	mov ss,qword [rsi]
	mov ds,qword [rsi]
	mov fs,qword [rsi]
	mov gs,qword [rsi]
%endif
	mov rax,es
	mov rax,cs
	mov rax,ss
	mov rax,ds
	mov rax,fs
	mov rax,gs
	mov eax,es
	mov eax,ss
	mov eax,ds
	mov eax,fs
	mov eax,fs
	mov ax,es
	mov ax,ss
	mov ax,ds
	mov ax,fs
	mov ax,gs
	mov [rdi],es
	mov [rdi],cs
	mov [rdi],ss
	mov [rdi],ds
	mov [rdi],fs
	mov [rdi],gs
	mov word [rdi],es
	mov word [rdi],cs
	mov word [rdi],ss
	mov word [rdi],ds
	mov word [rdi],fs
	mov word [rdi],gs
%ifdef ERR
	mov qword [rdi],es
	mov qword [rdi],cs
	mov qword [rdi],ss
	mov qword [rdi],ds
	mov qword [rdi],fs
	mov qword [rdi],gs
%endif
