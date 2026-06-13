	db %chr(40,41,42,43,44,45,46)
	db %ord("Hello, World!")
	db %ord("Hello, World!",1,-1)
	db %chr()
	db %b2hs("Hello, World!")
	db %b2hs("Hello, World!",':')
	db %hs2b("303132 33 34 35 3 6 3 78 9", "abcd")
	db 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09
	db %hs2b("00010203 4 0506 07 8","9")
