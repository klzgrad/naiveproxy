; Fun tests of the preprocessor indirection mode...

	bits 64

%assign foo1		11
%assign foo11		1111
%assign foo2		22
%assign foo22		2222
%assign foo3		33
%assign foo33		3333
%assign n		2
	dd	foo%[foo%[n]]*100
	dd	foo%[n]*100
%assign foo%[foo%[n]]	foo%[foo%[n]]*100
;%assign foo%[n]		foo%[n]*100

	dd	foo1
	dd	foo2
	dd	foo3
	dd	foo11
	dd	foo22
	dd	foo33

%define foo33bar	999999
	dd	%[foo%[foo3]bar]

%assign bctr 0
%macro bluttan 0
%assign bctr bctr+1
%assign bluttan%[bctr]	bctr
%defstr bstr bluttan%[bctr]
	db bluttan%[bctr]
	db bstr
%endmacro

%rep 20
	bluttan
%endrep
%rep 20
	db bluttan%[bctr]
%assign bctr bctr-1
%endrep
