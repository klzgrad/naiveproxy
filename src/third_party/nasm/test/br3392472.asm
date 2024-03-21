org 0
%ifndef SEEK
 %define SEEK 0
%endif
times SEEK - ($ - $$) nop
jmp near init

%ifndef NUM
 %define NUM 9956h
%endif
times NUM - ($ - $$) db 0
init:
