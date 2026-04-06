	section .pdata rdata align=2
	dd 1
	dd 2
	dd 3

	section .rdata align=16
	dd 4
	dd 5
	dd 6

	section ultra
	dd 10
	dd 11
	dd 12

	section infra rdata
	dd 20
	dd 21
	dd 22

	section omega rdata align=1
	dd 90
	dd 91
	dd 92

	section .xdata
	dd 7
	dd 8
	dd 9

	section ultra align=8
	dd 13
	dd 14
	dd 15

	section infra rdata align=1
	dd 23
	dd 24
	dd 25

	section omega rdata
	sectalign 2
	dd 93
	dd 94
	dd 95
