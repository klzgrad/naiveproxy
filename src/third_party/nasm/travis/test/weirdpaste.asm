	%define foo xyzzy
%define bar 1e+10

%define xyzzy1e 15

%macro dx 2
%assign	xx %1%2
	dw xx
%endmacro

	dx foo, bar

%macro df 2
%assign xy __float32__(%1e+%2)
	dd xy
	dd %1e+%2
%endmacro

	df 1, 36
	df 33, 20
	df 0, 2
	df 1.2, 5


%define N 1e%++%+ 5
	dd N, 1e+5

;
; test nil dereference, since we're
; modifying with %line keep it last
; in the file
;
; BR 3392696
;
%line 1 "`weirdpaste.asm"
mov eax, eax
