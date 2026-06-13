;
; floatx.asm
;
; Test hexadecimal floating-point numbers

%define Inf __Infinity__
%define NaN __QNaN__

; 16-bit
	dw 1.0
	dw 0x1.0
	dw 2.0
	dw 0x2.0
	dw 0x1.0p+1
	dw 0x1.0p-1
	dw 0x0.0
	dw 0x1.23456789
	dw 0x0.123456789
	dw 0x0.0000123456789
	dw 0x1.23456789p10
	dw 0x1.23456789p+10
	dw 0x1.23456789p-10
	dw 0x0.123456789p10
	dw 0x0.123456789p+10
	dw 0x0.123456789abcdef0123456789abcdef012345p-10
	dw 0x0.0000123456789
	dw 0x0.0000123456789p+10
	dw 0x0.0000123456789p-10
	dw 0x1.0p-25		; Underflow
	dw 0x1.01p-25		; Rounds to denorm
	dw 0x1.0p-24		; Smallest denorm
	dw 0x1.ffffffffffffffffffffffffffffp-16	; Rounds to denorm
	dw 0x1.0p-15		; Denorm
	dw 0x1.ffffffffffffffffffffffffffffp-15	; Rounds to normal
	dw 0x1.0p-14		; Smallest non-denorm
	dw 0x1.0p+15		; Biggest possible exponent
	dw 0x1.ffffffffffffffffffffffffffffp+15	; Rounds to infinity
	dw Inf			; Infinity
	dw NaN

; 32-bit
	dd 1.0
	dd 0x1.0
	dd 2.0
	dd 0x2.0
	dd 0x1.0p+1
	dd 0x1.0p-1
	dd 0x0.0
	dd 0x1.23456789
	dd 0x0.123456789
	dd 0x0.0000123456789
	dd 0x1.23456789p10
	dd 0x1.23456789p+10
	dd 0x1.23456789p-10
	dd 0x0.123456789p10
	dd 0x0.123456789p+10
	dd 0x0.123456789abcdef0123456789abcdef012345p-10
	dd 0x0.0000123456789
	dd 0x0.0000123456789p+10
	dd 0x0.0000123456789p-10
	dd 0x123456789.0
	dd 0x0000123456789.0
	dd 0x123456789.0p+0
	dd 0x123456789.0p+64
	dd 0x1.0p-150		; Underflow
	dd 0x1.01p-150		; Rounds to denorm
	dd 0x1.0p-149		; Smallest denorm
	dd 0x1.ffffffffffffffffffffffffffffp-128	; Rounds to denorm
	dd 0x1.0p-127		; Denorm
	dd 0x1.ffffffffffffffffffffffffffffp-127	; Rounds to normal
	dd 0x1.0p-126		; Smallest non-denorm
	dd 0x1.0p+127		; Biggest possible exponent
	dd 0x1.ffffffffffffffffffffffffffffp+127	; Rounds to infinity
	dd Inf			; Infinity
	dd NaN

; 64-bit
	dq 1.0
	dq 0x1.0
	dq 2.0
	dq 0x2.0
	dq 0x1.0p+1
	dq 0x1.0p-1
	dq 0x0.0
	dq 0x1.23456789
	dq 0x0.123456789
	dq 0x0.0000123456789
	dq 0x1.23456789p10
	dq 0x1.23456789p+10
	dq 0x1.23456789p-10
	dq 0x0.123456789p10
	dq 0x0.123456789p+10
	dq 0x0.123456789abcdef0123456789abcdef012345p-10
	dq 0x0.0000123456789
	dq 0x0.0000123456789p+10
	dq 0x0.0000123456789p-10
	dq 0x123456789.0
	dq 0x0000123456789.0
	dq 0x123456789.0p+0
	dq 0x123456789.0p+300
	dq 0x1.0p-1075		; Underflow
	dq 0x1.01p-1075		; Rounds to denorm
	dq 0x1.0p-1074		; Smallest denorm
	dq 0x1.ffffffffffffffffffffffffffffp-1024	; Rounds to denorm
	dq 0x1.0p-1023		; Denorm
	dq 0x1.ffffffffffffffffffffffffffffp-1023	; Rounds to normal
	dq 0x1.0p-1022		; Smallest non-denorm
	dq 0x1.0p+1023		; Biggest possible exponent
	dq 0x1.ffffffffffffffffffffffffffffp+1023	; Rounds to infinity
	dq Inf			; Infinity
	dq NaN

; 80-bit
	dt 1.0
	dt 0x1.0
	dt 2.0
	dt 0x2.0
	dt 0x1.0p+1
	dt 0x1.0p-1
	dt 0x0.0
	dt 0x1.23456789
	dt 0x0.123456789
	dt 0x0.0000123456789
	dt 0x1.23456789p10
	dt 0x1.23456789p+10
	dt 0x1.23456789p-10
	dt 0x0.123456789p10
	dt 0x0.123456789p+10
	dt 0x0.123456789abcdef0123456789abcdef012345p-10
	dt 0x0.0000123456789
	dt 0x0.0000123456789p+10
	dt 0x0.0000123456789p-10
	dt 0x123456789.0
	dt 0x0000123456789.0
	dt 0x123456789.0p+0
	dt 0x123456789.0p+1024
	dt 0x1.0p-16446		; Underflow
	dt 0x1.01p-16446	; Rounds to denorm
	dt 0x1.0p-16445		; Smallest denorm
	dt 0x1.ffffffffffffffffffffffffffffp-16384	; Rounds to denorm
	dt 0x1.0p-16383		; Denorm
	dt 0x1.ffffffffffffffffffffffffffffp-16383	; Rounds to normal
	dt 0x1.0p-16382		; Smallest non-denorm
	dt 0x1.0p+16383		; Biggest possible exponent
	dt 0x1.ffffffffffffffffffffffffffffp+16383	; Rounds to infinity
	dt Inf			; Infinity
	dt NaN

; 128-bit
	do 1.0
	do 0x1.0
	do 2.0
	do 0x2.0
	do 0x1.0p+1
	do 0x1.0p-1
	do 0x0.0
	do 0x1.23456789
	do 0x0.123456789
	do 0x0.0000123456789
	do 0x1.23456789p10
	do 0x1.23456789p+10
	do 0x1.23456789p-10
	do 0x0.123456789p10
	do 0x0.123456789p+10
	do 0x0.123456789abcdef0123456789abcdef012345p-10
	do 0x0.0000123456789
	do 0x0.0000123456789p+10
	do 0x0.0000123456789p-10
	do 0x123456789.0
	do 0x0000123456789.0
	do 0x123456789.0p+0
	do 0x123456789.0p+1024
	do 0x1.0p-16495		; Underflow
	do 0x1.01p-16495	; Rounds to denorm
	do 0x1.0p-16494		; Smallest denorm
	do 0x1.ffffffffffffffffffffffffffffffffp-16384	; Rounds to denorm
	do 0x1.0p-16383		; Denorm
	do 0x1.ffffffffffffffffffffffffffffffffp-16383	; Rounds to normal
	do 0x1.0p-16382		; Smallest non-denorm
	do 0x1.0p+16383		; Biggest possible exponent
	do 0x1.ffffffffffffffffffffffffffffffffp+16383	; Rounds to infinity
	do Inf			; Infinity
	do NaN

; 16-bit
	dw 1.0
	dw 0h1.0
	dw 2.0
	dw 0h2.0
	dw 0h1.0p+1
	dw 0h1.0p-1
	dw 0h0.0
	dw 0h1.23456789
	dw 0h0.123456789
	dw 0h0.0000123456789
	dw 0h1.23456789p10
	dw 0h1.23456789p+10
	dw 0h1.23456789p-10
	dw 0h0.123456789p10
	dw 0h0.123456789p+10
	dw 0h0.123456789abcdef0123456789abcdef012345p-10
	dw 0h0.0000123456789
	dw 0h0.0000123456789p+10
	dw 0h0.0000123456789p-10
	dw 0h1.0p-25		; Underflow
	dw 0h1.0p-24		; Smallest denorm
	dw 0h1.ffffffffffffffffffffffffffffp-16	; Rounds to denorm
	dw 0h1.0p-15		; Denorm
	dw 0h1.ffffffffffffffffffffffffffffp-15	; Rounds to normal
	dw 0h1.0p-14		; Smallest non-denorm
	dw 0h1.0p+15		; Biggest possible exponent
	dw 0h1.ffffffffffffffffffffffffffffp+15	; Rounds to infinity
	dw Inf			; Infinity
	dw NaN

; 32-bit
	dd 1.0
	dd 0h1.0
	dd 2.0
	dd 0h2.0
	dd 0h1.0p+1
	dd 0h1.0p-1
	dd 0h0.0
	dd 0h1.23456789
	dd 0h0.123456789
	dd 0h0.0000123456789
	dd 0h1.23456789p10
	dd 0h1.23456789p+10
	dd 0h1.23456789p-10
	dd 0h0.123456789p10
	dd 0h0.123456789p+10
	dd 0h0.123456789abcdef0123456789abcdef012345p-10
	dd 0h0.0000123456789
	dd 0h0.0000123456789p+10
	dd 0h0.0000123456789p-10
	dd 0h123456789.0
	dd 0h0000123456789.0
	dd 0h123456789.0p+0
	dd 0h123456789.0p+64
	dd 0h1.0p-150		; Underflow
	dd 0h1.0p-149		; Smallest denorm
	dd 0h1.ffffffffffffffffffffffffffffp-128	; Rounds to denorm
	dd 0h1.0p-127		; Denorm
	dd 0h1.ffffffffffffffffffffffffffffp-127	; Rounds to normal
	dd 0h1.0p-126		; Smallest non-denorm
	dd 0h1.0p+127		; Biggest possible exponent
	dd 0h1.ffffffffffffffffffffffffffffp+127	; Rounds to infinity
	dd Inf			; Infinity
	dd NaN

; 64-bit
	dq 1.0
	dq 0h1.0
	dq 2.0
	dq 0h2.0
	dq 0h1.0p+1
	dq 0h1.0p-1
	dq 0h0.0
	dq 0h1.23456789
	dq 0h0.123456789
	dq 0h0.0000123456789
	dq 0h1.23456789p10
	dq 0h1.23456789p+10
	dq 0h1.23456789p-10
	dq 0h0.123456789p10
	dq 0h0.123456789p+10
	dq 0h0.123456789abcdef0123456789abcdef012345p-10
	dq 0h0.0000123456789
	dq 0h0.0000123456789p+10
	dq 0h0.0000123456789p-10
	dq 0h123456789.0
	dq 0h0000123456789.0
	dq 0h123456789.0p+0
	dq 0h123456789.0p+300
	dq 0h1.0p-1075		; Underflow
	dq 0h1.0p-1074		; Smallest denorm
	dq 0h1.ffffffffffffffffffffffffffffp-1024	; Rounds to denorm
	dq 0h1.0p-1023		; Denorm
	dq 0h1.ffffffffffffffffffffffffffffp-1023	; Rounds to normal
	dq 0h1.0p-1022		; Smallest non-denorm
	dq 0h1.0p+1023		; Biggest possible exponent
	dq 0h1.ffffffffffffffffffffffffffffp+1023	; Rounds to infinity
	dq Inf			; Infinity
	dq NaN

; 80-bit
	dt 1.0
	dt 0h1.0
	dt 2.0
	dt 0h2.0
	dt 0h1.0p+1
	dt 0h1.0p-1
	dt 0h0.0
	dt 0h1.23456789
	dt 0h0.123456789
	dt 0h0.0000123456789
	dt 0h1.23456789p10
	dt 0h1.23456789p+10
	dt 0h1.23456789p-10
	dt 0h0.123456789p10
	dt 0h0.123456789p+10
	dt 0h0.123456789abcdef0123456789abcdef012345p-10
	dt 0h0.0000123456789
	dt 0h0.0000123456789p+10
	dt 0h0.0000123456789p-10
	dt 0h123456789.0
	dt 0h0000123456789.0
	dt 0h123456789.0p+0
	dt 0h123456789.0p+1024
	dt 0h1.0p-16446		; Underflow
	dt 0h1.0p-16445		; Smallest denorm
	dt 0h1.ffffffffffffffffffffffffffffp-16384	; Rounds to denorm
	dt 0h1.0p-16383		; Denorm
	dt 0h1.ffffffffffffffffffffffffffffp-16383	; Rounds to normal
	dt 0h1.0p-16382		; Smallest non-denorm
	dt 0h1.0p+16383		; Biggest possible exponent
	dt 0h1.ffffffffffffffffffffffffffffp+16383	; Rounds to infinity
	dt Inf			; Infinity
	dt NaN

; 128-bit
	do 1.0
	do 0h1.0
	do 2.0
	do 0h2.0
	do 0h1.0p+1
	do 0h1.0p-1
	do 0h0.0
	do 0h1.23456789
	do 0h0.123456789
	do 0h0.0000123456789
	do 0h1.23456789p10
	do 0h1.23456789p+10
	do 0h1.23456789p-10
	do 0h0.123456789p10
	do 0h0.123456789p+10
	do 0h0.123456789abcdef0123456789abcdef012345p-10
	do 0h0.0000123456789
	do 0h0.0000123456789p+10
	do 0h0.0000123456789p-10
	do 0h123456789.0
	do 0h0000123456789.0
	do 0h123456789.0p+0
	do 0h123456789.0p+1024
	do 0h1.0p-16495		; Underflow
	do 0h1.0p-16494		; Smallest denorm
	do 0h1.ffffffffffffffffffffffffffffffffp-16384	; Rounds to denorm
	do 0h1.0p-16383		; Denorm
	do 0h1.ffffffffffffffffffffffffffffffffp-16383	; Rounds to normal
	do 0h1.0p-16382		; Smallest non-denorm
	do 0h1.0p+16383		; Biggest possible exponent
	do 0h1.ffffffffffffffffffffffffffffffffp+16383	; Rounds to infinity
	do Inf			; Infinity
	do NaN

; 16-bit
	dw 1.0
	dw $1.0
	dw 2.0
	dw $2.0
	dw $1.0p+1
	dw $1.0p-1
	dw $0.0
	dw $1.23456789
	dw $0.123456789
	dw $0.0000123456789
	dw $1.23456789p10
	dw $1.23456789p+10
	dw $1.23456789p-10
	dw $0.123456789p10
	dw $0.123456789p+10
	dw $0.123456789abcdef0123456789abcdef012345p-10
	dw $0.0000123456789
	dw $0.0000123456789p+10
	dw $0.0000123456789p-10
	dw $1.0p-25		; Underflow
	dw $1.0p-24		; Smallest denorm
	dw $1.ffffffffffffffffffffffffffffp-16	; Rounds to denorm
	dw $1.0p-15		; Denorm
	dw $1.ffffffffffffffffffffffffffffp-15	; Rounds to normal
	dw $1.0p-14		; Smallest non-denorm
	dw $1.0p+15		; Biggest possible exponent
	dw $1.ffffffffffffffffffffffffffffp+15	; Rounds to infinity
	dw Inf			; Infinity
	dw NaN

; 32-bit
	dd 1.0
	dd $1.0
	dd 2.0
	dd $2.0
	dd $1.0p+1
	dd $1.0p-1
	dd $0.0
	dd $1.23456789
	dd $0.123456789
	dd $0.0000123456789
	dd $1.23456789p10
	dd $1.23456789p+10
	dd $1.23456789p-10
	dd $0.123456789p10
	dd $0.123456789p+10
	dd $0.123456789abcdef0123456789abcdef012345p-10
	dd $0.0000123456789
	dd $0.0000123456789p+10
	dd $0.0000123456789p-10
	dd $123456789.0
	dd $0000123456789.0
	dd $123456789.0p+0
	dd $123456789.0p+64
	dd $1.0p-150		; Underflow
	dd $1.0p-149		; Smallest denorm
	dd $1.ffffffffffffffffffffffffffffp-128	; Rounds to denorm
	dd $1.0p-127		; Denorm
	dd $1.ffffffffffffffffffffffffffffp-127	; Rounds to normal
	dd $1.0p-126		; Smallest non-denorm
	dd $1.0p+127		; Biggest possible exponent
	dd $1.ffffffffffffffffffffffffffffp+127	; Rounds to infinity
	dd Inf			; Infinity
	dd NaN

; 64-bit
	dq 1.0
	dq $1.0
	dq 2.0
	dq $2.0
	dq $1.0p+1
	dq $1.0p-1
	dq $0.0
	dq $1.23456789
	dq $0.123456789
	dq $0.0000123456789
	dq $1.23456789p10
	dq $1.23456789p+10
	dq $1.23456789p-10
	dq $0.123456789p10
	dq $0.123456789p+10
	dq $0.123456789abcdef0123456789abcdef012345p-10
	dq $0.0000123456789
	dq $0.0000123456789p+10
	dq $0.0000123456789p-10
	dq $123456789.0
	dq $0000123456789.0
	dq $123456789.0p+0
	dq $123456789.0p+300
	dq $1.0p-1075		; Underflow
	dq $1.0p-1074		; Smallest denorm
	dq $1.ffffffffffffffffffffffffffffp-1024	; Rounds to denorm
	dq $1.0p-1023		; Denorm
	dq $1.ffffffffffffffffffffffffffffp-1023	; Rounds to normal
	dq $1.0p-1022		; Smallest non-denorm
	dq $1.0p+1023		; Biggest possible exponent
	dq $1.ffffffffffffffffffffffffffffp+1023	; Rounds to infinity
	dq Inf			; Infinity
	dq NaN

; 80-bit
	dt 1.0
	dt $1.0
	dt 2.0
	dt $2.0
	dt $1.0p+1
	dt $1.0p-1
	dt $0.0
	dt $1.23456789
	dt $0.123456789
	dt $0.0000123456789
	dt $1.23456789p10
	dt $1.23456789p+10
	dt $1.23456789p-10
	dt $0.123456789p10
	dt $0.123456789p+10
	dt $0.123456789abcdef0123456789abcdef012345p-10
	dt $0.0000123456789
	dt $0.0000123456789p+10
	dt $0.0000123456789p-10
	dt $123456789.0
	dt $0000123456789.0
	dt $123456789.0p+0
	dt $123456789.0p+1024
	dt $1.0p-16446		; Underflow
	dt $1.0p-16445		; Smallest denorm
	dt $1.ffffffffffffffffffffffffffffp-16384	; Rounds to denorm
	dt $1.0p-16383		; Denorm
	dt $1.ffffffffffffffffffffffffffffp-16383	; Rounds to normal
	dt $1.0p-16382		; Smallest non-denorm
	dt $1.0p+16383		; Biggest possible exponent
	dt $1.ffffffffffffffffffffffffffffp+16383	; Rounds to infinity
	dt Inf			; Infinity
	dt NaN

; 128-bit
	do 1.0
	do $1.0
	do 2.0
	do $2.0
	do $1.0p+1
	do $1.0p-1
	do $0.0
	do $1.23456789
	do $0.123456789
	do $0.0000123456789
	do $1.23456789p10
	do $1.23456789p+10
	do $1.23456789p-10
	do $0.123456789p10
	do $0.123456789p+10
	do $0.123456789abcdef0123456789abcdef012345p-10
	do $0.0000123456789
	do $0.0000123456789p+10
	do $0.0000123456789p-10
	do $123456789.0
	do $0000123456789.0
	do $123456789.0p+0
	do $123456789.0p+1024
	do $1.0p-16495		; Underflow
	do $1.0p-16494		; Smallest denorm
	do $1.ffffffffffffffffffffffffffffffffp-16384	; Rounds to denorm
	do $1.0p-16383		; Denorm
	do $1.ffffffffffffffffffffffffffffffffp-16383	; Rounds to normal
	do $1.0p-16382		; Smallest non-denorm
	do $1.0p+16383		; Biggest possible exponent
	do $1.ffffffffffffffffffffffffffffffffp+16383	; Rounds to infinity
	do Inf			; Infinity
	do NaN
