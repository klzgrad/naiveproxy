;Testname=test;  Arguments=-fbin -outf.bin;         Files=stdout stderr utf.bin
;Testname=error; Arguments=-fbin -outf.bin -DERROR; Files=stdout stderr utf.bin
%define u(x) __utf16__(x)
%define w(x) __utf32__(x)
%define ul(x) __utf16le__(x)
%define wl(x) __utf32le__(x)
%define ub(x) __utf16be__(x)
%define wb(x) __utf32be__(x)

	db `Test \u306a\U0001abcd\n`
	dw u(`Test \u306a\U0001abcd\n`)
	dd w(`Test \u306a\U0001abcd\n`)

	db `\u306a`
	db `\xe3\x81\xaa`

	dw __utf16__ "Hello, World!"

	nop

	mov ax,u(`a`)
	mov bx,u(`\u306a`)
	mov cx,u(`\xe3\x81\xaa`)
	mov eax,u(`ab`)
	mov ebx,u(`\U0001abcd`)
	mov ecx,w(`\U0001abcd`)

	db `Test \u306a\U0001abcd\n`
	dw ul(`Test \u306a\U0001abcd\n`)
	dd wl(`Test \u306a\U0001abcd\n`)

	db `\u306a`
	db `\xe3\x81\xaa`

	dw __utf16le__ "Hello, World!"

	nop

	mov ax,ul(`a`)
	mov bx,ul(`\u306a`)
	mov cx,ul(`\xe3\x81\xaa`)
	mov eax,ul(`ab`)
	mov ebx,ul(`\U0001abcd`)
	mov ecx,wl(`\U0001abcd`)
	
	db `Test \u306a\U0001abcd\n`
	dw ub(`Test \u306a\U0001abcd\n`)
	dd wb(`Test \u306a\U0001abcd\n`)

	db `\u306a`
	db `\xe3\x81\xaa`

	dw __utf16be__ "Hello, World!"

	nop

	mov ax,ub(`a`)
	mov bx,ub(`\u306a`)
	mov cx,ub(`\xe3\x81\xaa`)
	mov eax,ub(`ab`)
	mov ebx,ub(`\U0001abcd`)
	mov ecx,wb(`\U0001abcd`)

%ifdef ERROR
	dw __utf16__ 33
	dw __utf16__, 46
	dw __utf16__("Hello, World!",16)
	dw __utf16__("Hello, World!",16
	dw u(`\xff`)

	dw __utf16le__ 33
	dw __utf16le__, 46
	dw __utf16le__("Hello, World!",16)
	dw __utf16le__("Hello, World!",16
	dw ul(`\xff`)

	dw __utf16be__ 33
	dw __utf16be__, 46
	dw __utf16be__("Hello, World!",16)
	dw __utf16be__("Hello, World!",16
	dw ub(`\xff`)
%endif
