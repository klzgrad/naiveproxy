	bits 64
_start:
	es add eax,eax
	add eax,[fs:rdx]
	ds jz foo
	cs jmp bar
foo:
	hlt
bar:
	udb

%fatal "Kill me now"
