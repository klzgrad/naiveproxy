	bits 64
	default rel

%use altreg

	section .text

bar	equ 0xcc

	;; Each instruction group separated by blank should encode identially

	;; k_mov kreg size_suffix size_name gpr big_gpr
%macro k_mov 5
	kmov%2 %1,[foo]
	kmov %1,%3 [foo]
	kmov %3 [foo],%1

	kmov%2 [foo],%1
	kmov  %3 [foo],%1
	kmov  [foo],%3 %1
	kmov  %3 [foo],%1

	kmov%2 %1,%1
	kmov   %3 %1,%1
	kmov   %1,%3 %1

	kmov%2 %1,%4
	kmov%2 %1,%5
	kmov   %1,%4
	kmov   %3 %1,%4
	kmov   %3 %1,%5
	kmov   %1,%3 %4
    %ifidni %4,%5
	kmov   %1,%5
    %endif

	kmov%2 %5,%1
    %ifidni %4,%5
	kmov   %5,%1
    %endif
	kmov   %5,%3 %1

%endmacro

;%pragma list options +bempf

	;; k_rr op kreg size_suffix size_name
%macro k_rr 4
	%1%3 %2,%2
	%1  %4 %2,%2
	%1  %2,%4 %2

%endmacro

	;; k_rri op kreg size_suffix size_name
%macro k_rrr 4
	%1%3 %2,%2,%2
	%1  %4 %2,%2,%2
	%1  %2,%4 %2,%2
	%1  %2,%2,%4 %2

%endmacro

	;; k_rri op kreg size_suffix size_name
%macro k_rri 4
	%1%3 %2,%2,bar
	%1  %4 %2,%2,bar
	%1  %2,%4 %2,bar

%endmacro

%define size_b byte
%define size_w word
%define size_d dword
%define size_q qword

%define gpr_b  b
%define gpr_w  w
%define gpr_d  d
%define gpr_q

%define bgpr_b d
%define bgpr_w d
%define bgpr_d d
%define bgpr_q

	;; k_test size_suffix regno
%macro k_test 2
	k_mov k%2,%1,size_%1,%[r %+ %2 %+ gpr_%1],%[r %+ %2 %+ bgpr_%1]
	k_rrr kadd,k%2,%1,size_%1
	k_rrr kand,k%2,%1,size_%1
	k_rrr kandn,k%2,%1,size_%1
	k_rrr kand,k%2,%1,size_%1
	k_rr knot,k%2,%1,size_%1
	k_rrr kor,k%2,%1,size_%1
	k_rr kortest,k%2,%1,size_%1
	k_rri kshiftl,k%2,%1,size_%1
	k_rri kshiftr,k%2,%1,size_%1
	k_rr ktest,k%2,%1,size_%1
	k_rrr kxnor,k%2,%1,size_%1
	k_rrr kxor,k%2,%1,size_%1
%endmacro

%assign nreg 0
%define kreg k %+ nreg
%rep 8

	k_test b,nreg
	k_test w,nreg
	k_test d,nreg
	k_test q,nreg

	kunpckbw kreg,kreg,kreg
	kunpck   word kreg,kreg,kreg
	kunpck   kreg,byte kreg,kreg
	kunpck   kreg,kreg,byte kreg
	kunpck   word kreg,byte kreg,kreg
	kunpck   word kreg,kreg,byte kreg

	kunpckwd kreg,kreg,kreg
	kunpck   dword kreg,kreg,kreg
	kunpck   kreg,word kreg,kreg
	kunpck   kreg,kreg,word kreg
	kunpck   dword kreg,word kreg,kreg
	kunpck   dword kreg,kreg,word kreg

	kunpckdq kreg,kreg,kreg
	kunpck   qword kreg,kreg,kreg
	kunpck   kreg,dword kreg,kreg
	kunpck   kreg,kreg,dword kreg
	kunpck   qword kreg,dword kreg,kreg
	kunpck   qword kreg,kreg,dword kreg

	%assign nreg nreg+1
%endrep

	section .bss

foo	resq 1
