%ifidn __OUTPUT_FORMAT__, bin
	msg_format: db 'This is binary format file'
%elifidn __OUTPUT_FORMAT__, elf32
	msg_format: db 'This is elf32 format file'
%elifidn __OUTPUT_FORMAT__, elf64
	msg_format: db 'This is elf64 format file'
%elifidn __OUTPUT_FORMAT__, macho32
	msg_format: db 'This is macho32 format file'
%elifidn __OUTPUT_FORMAT__, macho64
	msg_format: db 'This is macho64 format file'
%elifidn __OUTPUT_FORMAT__, aout
	msg_format: db 'This is aout format file'
%elifidn __OUTPUT_FORMAT__, win32
	msg_format: db 'This is win32 format file'
%elifidn __OUTPUT_FORMAT__, ieee
	msg_format: db 'This is ieee format file'
%else
	msg_format: db 'This is some other format file'
%endif
