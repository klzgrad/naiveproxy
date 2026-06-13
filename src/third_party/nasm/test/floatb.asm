;Testname=unoptimized; Arguments=-O0 -fbin -ofloatb.bin; Files=stdout stderr floatb.bin
;Testname=optimized;   Arguments=-Ox -fbin -ofloatb.bin; Files=stdout stderr floatb.bin

	;; Known problematic floating-point numbers and their proper
	;; encoding...

	bits 64

	dd 1.1e10
	dd 0x5023e9ac	; Should be...

	dd 50.40e9
	dd 0x513bc130	; Should be...

	dq 1.4e23
	dq 0x44bda56a4b0835c0

	dq 50.48e21
	dq 0x44a5610d7502feae

	dt 1.2e28
	dq 0x9b18ab5df7180b6c
	dw 0x405c

	dt 50.46e25
	dq 0xd0b29a67e95dcb60
	dw 0x4057

	do 0xf.ffffff8p-4
	do 0q3.7777777774p-2
	do 0b1.1111_1111_1111_1111_1111_1111_1111_111p-1

	dt 0xf.ffffff8p-4
	dt 0q3.7777777774p-2
	dt 0b1.1111_1111_1111_1111_1111_1111_1111_111p-1

	dq 0xf.ffffff8p-4
	dq 0q3.7777777774p-2
	dq 0b1.1111_1111_1111_1111_1111_1111_1111_111p-1

	dd 0xf.ffffff8p-4
	dd 0q3.7777777774p-2
	dd 0b1.1111_1111_1111_1111_1111_1111_1111_111p-1

	dw 0xf.ffffff8p-4
	dw 0q3.7777777774p-2
	dw 0b1.1111_1111_1111_1111_1111_1111_1111_111p-1

	db 0xf.ffffff8p-4
	db 0q3.7777777774p-2
	db 0b1.1111_1111_1111_1111_1111_1111_1111_111p-1


	;; Way too big numbers, should overflow to +Inf
	dd 1.0E646456955
	dd 1.0E646456956
	dd 1.0E2147483646
	dd 1.0E2147483647
	dd 1.0E2147483648
	dd 1.0E2147483649
