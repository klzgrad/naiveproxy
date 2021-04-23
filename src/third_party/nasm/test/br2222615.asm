;Testname=noerror; Arguments=-fbin -obr2222615.bin; Files=stdout stderr br2222615.bin
;Testname=error; Arguments=-DERROR -fbin -obr2222615.bin; Files=stdout stderr br2222615.bin

%macro bluttan 0
	nop
%endmacro

%ifnmacro bluttan
 %error "bluttan is a macro"
%endif

%ifmacro blej
 %error "blej is not a macro"
%endif

%ifdef ERROR
 %ifnmacro
 %endif
%endif
