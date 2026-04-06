;Testname=test; Arguments=-fbin -omovnti.bin; Files=stdout stderr movnti.bin
; BR 2028995

	bits 16
	movnti [si],eax
	bits 32
	movnti [esi],eax
	bits 64
	movnti [rsi],eax
	movnti [rsi],rax
