	bits 64
	default rel

%ifndef N
  %define N 128
%endif

	%macro tms 1+
%ifdef DO_TIMES
	times N %1
%else
  %assign %%n 0
    %rep N
%%l %+ %%n:
      %assign %%n %%n + 1
	%1
    %endrep
  %endif
%endmacro

baz:
	nop
bar:
	tms jmp baz
	tms jmp quux
quux:

	ret
