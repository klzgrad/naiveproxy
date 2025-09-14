;Testname=preproc; Arguments=-E; Files=stdout stderr
;Testname=bin; Arguments=-fbin -oweirdpaste.bin; Files=stdout stderr weirdpaste.bin

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
