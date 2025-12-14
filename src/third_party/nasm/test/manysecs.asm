%ifndef NSECS
  %assign NSECS 16384
%endif

%assign NSECS ((NSECS+3) & ~3)

%assign n 0
%rep NSECS
  %assign gcom (n & ~3)	+ 2
	section .text %+ n progbits exec
start_ %+ n:
	nop
	jmp start_ %+ gcom
  %assign n n+1
%endrep
