	dd 0x1234_5678
	dd 305_419_896		; Same number as above it
	dd 0x1e16		; NOT a floating-point number!
	dd 1e16h		; NOT a floating-point number!
	dd 1e16_h		; NOT a floating-point number!
	dd $1e16		; NOT a floating-point number!
	dd $1e+16		; NOT a floating-point number!
	dd 1e16			; THIS is a floating-point number!
	dd 1e+16
	dd 1.e+16
	dd 1e+1_6
	dd 1e1_6
	dd 1.0e16
	dd 1_0e16		; This is 1e17, not 1e16!
