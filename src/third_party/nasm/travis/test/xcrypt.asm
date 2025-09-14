;Testname=test; Arguments=-fbin -oxcrypt.bin; Files=stdout stderr xcrypt.bin
; BR 2029829

	bits 32

	rep xstore
	rep xcryptecb
	rep xcryptcbc
	rep xcryptctr
	rep xcryptcfb
	rep xcryptofb
	rep montmul
	rep xsha1
	rep xsha256

	xstore
	xcryptecb
	xcryptcbc
	xcryptctr
	xcryptcfb
	xcryptofb
	montmul
	xsha1
	xsha256
