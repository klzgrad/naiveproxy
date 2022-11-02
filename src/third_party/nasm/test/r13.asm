;Testname=test; Arguments=-fbin -or13.bin; Files=stdout stderr r13.bin

	bits 64
	mov rax,[rbx]
	mov rax,[rbx*4]
	mov rax,[rbx+rbx*2]
	mov rax,[r13+rbx*2]
	mov rax,[rbp]
	mov rax,[rbp*4]
	mov rax,[rbp+rbp*2]
	mov rax,[rbp+r13*2]
	mov rax,[r13]
	mov rax,[r13*4]
	mov rax,[r13+rbp*2]
	mov rax,[r13+r13*2]
