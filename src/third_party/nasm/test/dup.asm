	bits 32

	db 33
	db (44)
;	db (44,55)	-- error
	db %(44,55)
	db %('XX','YY')
	db ('AA')
	db %('BB')
	db ?
	db 6 dup (33)
	db 6 dup (33, 34)
	db 6 dup (33, 34), 35
	db 7 dup (99)
	db 7 dup dword (?, word ?,?)
	dw byte (?,44)

	dw 3 dup (0xcc, 4 dup byte ('PQR'), ?), 0xabcd

	dd 16 dup (0xaaaa, ?, 0xbbbbbb)
	dd 64 dup (?)

	resb 1
	resb 2
	resb 4
	resb 8

	resw 1
	resw 2
	resw 4
	resw 8

	resq 1
	resq 2
	resq 4
	resq 8
