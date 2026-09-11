;Testname=test; Arguments=-fbin -oistruc_local.bin; Files=stdout stderr istruc_local.bin

struc Struc1
    .dword: resd 1
    .word: resw 1
endstruc

struc Struc2
    .word: resw 1
    .dword: resd 1
endstruc

; The following returned error about negative values for TIMES in nasm 2.15.05
; because local labels seemingly matching Struc1 have been replaced by those in
; Struc2.

istruc Struc1
    at .dword, dd 0xffffffff
    at .word, dw 0x1111
iend

; The following two just didn't work as istruc was just literally outputting
; local labels which are unknown after a global label appears.

struc1:

istruc Struc1
    at .dword, dd 0x78563412
    at .word, dw 0xbc9a
iend

struc2:

istruc Struc2
    at .word, dw 0xbc9a
    at .dword, dd 0x78563412
iend
