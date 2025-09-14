%macro mmac 0
%rep 1
%endrep
%endmacro
	mmac

%macro mmac1 0
%rep 1
%rep 1
	mmac
%endrep
%endrep
%endmacro

%macro mmac2 0
%rep 1

%endrep
%endmacro
	mmac2

%macro mmac3 1
%rep 1
%rep 1
%endrep
%endrep
%endmacro
	mmac3 x

%macro mmac4 0
%rep 1
%rep 1
	mmac3 x
%endrep
%endrep
%endmacro
	mmac4

%macro mmac5 0
%rep 1
%rep 1
nop
%endrep
%endrep
%endmacro
	mmac5

%macro mmac6 0
%endmacro
	mmac6

%macro mmac7 0
	mmac6
%endmacro
	mmac7

%macro mmac8 0
%rep 1
	mmac6
%endrep
%endmacro
	mmac8

%rep 1
	mmac3 y
%endrep
