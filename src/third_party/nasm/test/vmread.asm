;Testname=test; Arguments=-fbin -ovmread.bin; Files=stdout stderr vmread.bin

	bits 32
	vmread dword [0], eax
	vmwrite eax, dword [0]
	vmread [0], eax
	vmwrite eax, [0]

	bits 64
	vmread qword [0], rax
	vmwrite rax, qword [0]
	vmread [0], rax
	vmwrite rax, [0]

%ifdef ERROR
	bits 32
	vmread qword [0], eax
	vmwrite eax, qword [0]

	bits 64
	vmread dword [0], eax
	vmwrite eax, dword [0]

	vmread qword [0], eax
	vmwrite eax, qword [0]
%endif