;
; this is a for BR3005117
; http://sourceforge.net/tracker/?func=detail&aid=3005117&group_id=6208&atid=106208
;
%macro b_struc 1-*
    %push foo
        %define %$strucname %1
%%top_%$strucname:
        %rep %0 - 1
            %rotate 1
            resb %{$strucname}%1 - ($ - %%top_%$strucname)
%1:
        %endrep
        resb %{$strucname}_size - ($ - %%top_%$strucname)
    %pop
%endmacro

struc timeval
    .tv_sec     resd    1
    .tv_usec    resd    1
endstruc

section .text
    mov [timeval_struct.tv_sec], eax

section .bss
    timeval_struct b_struc timeval, .tv_sec, .tv_usec
        timeval_struct_len equ $ - timeval_struct

section .text

;
; this is a test for BR3026808
; http://sourceforge.net/tracker/?func=detail&aid=3026808&group_id=6208&atid=106208
;
%imacro proc 1
    %push proc
    %assign %$arg 1
%endmacro

%imacro arg 0-1 1
    %assign %$arg %1+%$arg
%endmacro

%imacro endproc 0
    %pop
%endmacro

proc Test
    %$ARG arg
endproc

;
; this is a test for BR3066383
; http://sourceforge.net/tracker/?func=detail&aid=3066383&group_id=6208&atid=106208
;
%macro pp_local 1
    %push
        %assign %$_uses 0
        %rep 4
            %assign %$_ur%$_uses %$_uses
            mov ecx, %$_ur%$_uses
            %assign %$_uses %$_uses+1
        %endrep
    %pop
%endmacro

pp_local 1
