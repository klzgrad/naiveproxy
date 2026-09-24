bits 32

; Simple struc example
struc teststruc1
	.long: resd 1
	.word: resw 1
	.byte: resb 1
	.str:  resb 32
endstruc

; Reference with offset
mov [ebp - 40 + teststruc1.word], ax

istruc teststruc1
	at .word, db 5
iend

; Struc with base offset
; should be the same as the previous stuc
struc teststruc2, -40
	.long: resd 1
	.word: resw 1
	.byte: resb 1
	.str:  resb 32
endstruc

mov [ebp + teststruc2.word], ax

istruc teststruc2
	at .word, db 5
iend
