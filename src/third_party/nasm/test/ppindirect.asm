;Testname=test; Arguments=-E -o ppindirect.out; Files=ppindirect.out

; Fun tests of the preprocessor indirection mode...

%assign foo1		11
%assign foo11		1111
%assign foo2		22
%assign foo22		2222
%assign foo3		33
%assign foo33		3333
%assign n		2
foo%[foo%[n]]*100
foo%[n]*100
%assign foo%[foo%[n]]	foo%[foo%[n]]*100
;%assign foo%[n]		foo%[n]*100

	foo1
	foo2
	foo3
	foo11
	foo22
	foo33

%define foo33bar	999999
	%[foo%[foo3]bar]
	
%assign bctr 0
%macro bluttan 0
%assign bctr bctr+1
%assign bluttan%[bctr]	bctr
%defstr bstr bluttan%[bctr]
	bluttan%[bctr]
	bstr
%endmacro

%rep 20
	bluttan
%endrep
%rep 20
	bluttan%[bctr]
%assign bctr bctr-1
%endrep
