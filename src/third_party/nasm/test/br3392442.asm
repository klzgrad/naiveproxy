	;; Bug report 3392442: invalid warning

	and byte [0], ~80h
	and byte [0], 0xfff
	and byte [0], -256
	and byte [0], -257
