;Testname=test; Arguments=-fbin -opopcnt.bin; Files=stdout stderr popcnt.bin

	bits 16

	popcnt ax,cx
	popcnt ax,[si]
	popcnt ax,word [si]
	popcnt eax,ecx
	popcnt eax,[si]
	popcnt eax,dword [si]

	bits 32

	popcnt ax,cx
	popcnt ax,[esi]
	popcnt ax,word [esi]
	popcnt eax,ecx
	popcnt eax,[esi]
	popcnt eax,dword [esi]

	bits 64

	popcnt ax,cx
	popcnt ax,[rsi]
	popcnt ax,word [rsi]
	popcnt eax,ecx
	popcnt eax,[rsi]
	popcnt eax,dword [rsi]
	popcnt rax,rcx
	popcnt rax,[rsi]
	popcnt rax,qword [rsi]
	