;Testname=test; Arguments=-fbin -olarlsl.bin; Files=stdout stderr larlsl.bin

	bits 64

	lar ax,bx
	lar ax,[rsi]
	lar ax,word [rsi]
	lar eax,bx
	lar eax,[rsi]
	lar eax,word [rsi]
	lar rax,bx
	lar rax,[rsi]
	lar rax,word [rsi]

	lsl ax,bx
	lsl ax,[rsi]
	lsl ax,word [rsi]
	lsl eax,bx
	lsl eax,[rsi]
	lsl eax,word [rsi]
	lsl rax,bx
	lsl rax,[rsi]
	lsl rax,word [rsi]
