%ifidn __OUTPUT_FORMAT__, bin
	msg_format: db 'This is binary format file'
%elifidn __OUTPUT_FORMAT__, elf32
	section .rodata
	msg_format: db 'This is elf32 format file'
%elifidn __OUTPUT_FORMAT__, elf64
	section .rodata
	msg_format: db 'This is elf64 format file'
%elifidn __OUTPUT_FORMAT__, macho32
	section .rodata
	msg_format: db 'This is macho32 format file'
%elifidn __OUTPUT_FORMAT__, macho64
	section .rodata
	msg_format: db 'This is macho64 format file'
%else
	msg_format: db 'This is some other format file'
%endif
