%idefine d dword
%define _1digits_nocheck(d)	(((d)% 10)+'0')
%xdefine _1digits(d)		(!!(d/10)*(1<<32)+  _1digits_nocheck(d))

	db _1digits(8)		; Should be 0x38

%define n 0x21
%xdefine ctr n
%define n 0x22

	db ctr, n		; Should be 0x21, 0x22

%define MNSUFFIX
%define MNCURRENT TEST%[MNSUFFIX]
%xdefine var MNCURRENT
