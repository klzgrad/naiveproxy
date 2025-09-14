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
