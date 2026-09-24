; The FIRST definition of prefixes win, so more specific first


%pragma win64  gprefix W64_

%pragma win    gprefix W_

%pragma elf    gprefix
%pragma elf    lprefix .L.

%pragma output gprefix _
%pragma output lprefix L.

	extern malloc

	global call_malloc
call_malloc:
	call malloc
	ret

myfunc:
	jmp call_malloc
