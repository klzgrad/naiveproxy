	[bits 64]

	;
	; HIT: Maximum possible value
	%assign i 0
	%rep 1000000
		mov rax, i
		%assign i i+1
		%if i == 2
			%exitrep
		%endif
	%endrep

	;
	; MISS: It's negative
	%assign i 0
	%rep 0xffffFFFFffffFFFE
		mov rax, 0xffffFFFFffffFFFE
		%assign i i+1
		%if i == 2
			%exitrep
		%endif
	%endrep

	;
	; MISS: It's negative
	%assign i 0
	%rep 0xffffFFFFffffFFFF
		db i
		%assign i i+1
		%if i == 2
			%exitrep
		%endif
	%endrep

	;
	; MISS: It's negative
	%assign i 0
	%rep -2
		db i
		%assign i i+1
		%if i == 2
			%exitrep
		%endif
	%endrep

	;
	; MISS: It's negative
	%assign i 0
	%rep -1
		db i
		%assign i i+1
		%if i == 2
			%exitrep
		%endif
	%endrep
