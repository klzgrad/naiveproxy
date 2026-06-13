%macro testrep 0-1
  %assign i 1
  %rep %1 4
    mov eax,i
    %if i==3
	%exitrep
       %if bull!shit		; Syntax error if evaluated
	nop
       %endif
       %rep other::bullshit	; Same
	nop
       %endrep
    %endif
    mov ebx,i
    %note in %?%1 iteration i
    %if i >= 3
	%error iteration i should not be seen
    %endif
    %assign i i+1
  %endrep
  ret
%endmacro

%macro testrep_nl 0-1.nolist
  %assign i 1
  %rep %1 4
    mov eax,i
    %if i==3
      %exitrep
    %endif
    %note in %?%1 iteration i
    mov ebx,i
    %if i >= 3
	%error iteration i should not be seen
    %endif
    %assign i i+1
  %endrep
  ret
%endmacro


	testrep
	testrep .nolist

	testrep_nl
	testrep_nl .nolist
