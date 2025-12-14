	bits 32

	db 33
	db (44)
;	db (44,55)	-- error
	db %(44.55)
	db %('XX','YY')
	db ('AA')
	db %('BB')
	db ?
	db 6 dup (33)
	db 6 dup (33, 34)
	db 6 dup (33, 34), 35
	db 7 dup (99)
	db 7 dup (?,?)
	dw byte (?,44)

	dw 0xcc, 4 dup byte ('PQR'), ?, 0xabcd

	dd 16 dup (0xaaaa, ?, 0xbbbbbb)
	dd 64 dup (?)
