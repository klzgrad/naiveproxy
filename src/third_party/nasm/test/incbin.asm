	db '*** ONCE ***', 0Ah
	incbin "incbin.data",32

	section more start=0x1000000
	db '*** TWELVE ***', 0Ah
	times 12 incbin "incbin.data",32
	db '<END>', 0Ah
