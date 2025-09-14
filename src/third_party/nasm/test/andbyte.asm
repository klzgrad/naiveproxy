;Testname=test; Arguments=-fbin -oandbyte.bin; Files=stdout stderr andbyte.bin
;Testname=otest; Arguments=-Ox -fbin -oandbyte.bin; Files=stdout stderr andbyte.bin

	bits 16

	add sp, byte -0x10
	add sp, -0x10
	adc sp, byte -0x10
	adc sp, -0x10
	and sp, byte -0x10
	and sp, -0x10
	sbb sp, byte -0x10
	sbb sp, -0x10
	sub sp, byte -0x10
	sub sp, -0x10
