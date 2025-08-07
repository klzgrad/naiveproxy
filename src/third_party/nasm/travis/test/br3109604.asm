	bits 64
b0:	vmovd xmm2, [rdx+r9]
e0:

	section .data
len:	dd e0 - b0		; Should be 6
