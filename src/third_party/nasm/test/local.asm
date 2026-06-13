;Testname=test; Arguments=-fbin -olocal.bin; Files=stdout stderr local.bin
	bits 32

%push bluttan

%define %$localsize 0

%stacksize flat
%local l1:qword, l2:dword, l3:dword, l4:qword
%arg a1:qword, a2:dword, a3:dword, a4:qword

	mov eax,[a1]
	mov ebx,[a2]
	mov ecx,[a3]
	mov edx,[a4]
	mov [l1],eax
	mov [l2],ebx
	mov [l3],ecx
	mov [l4],edx
