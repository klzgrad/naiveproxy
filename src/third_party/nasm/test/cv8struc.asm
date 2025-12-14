struc A_STRUC
  ._a: resw 1
endstruc

a_struc:
  istruc A_STRUC
  at A_STRUC._a, dw 1
  iend

	section .data
foo:
	dd 0x11111111
.bar:
	dd 0x22222222
