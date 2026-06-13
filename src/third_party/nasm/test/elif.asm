;Testname=unoptimized; Arguments=-O0 -fobj -oelif.obj; Files=stdout stderr elif.obj
;Testname=optimized;   Arguments=-Ox -fobj -oelif.obj; Files=stdout stderr elif.obj

%macro DosPrintMsg 1+
%ifnid %1
section .data
%%str_to_print:db %1
section .text
 mov dx,%%str_to_print
 mov ah,9
 int 0x21
%else
 mov dx,(%1)
 mov ah,9
 int 0x21
%endif
%endmacro

%macro DosExit 1
%if (%1) == 0
;use short-form return 0 exit
 int 0x20
%elif ((%1) < 256) && ((%1) > 0)
 mov ax,0x4C00 | (%1)
 int 0x21
%else
%error Invalid return value
%endif
%endmacro

org 0x100
 DosPrintMsg predefined_str
 DosPrintMsg "Using string with macro-defined label",10,0
 DosExit 0
 DosExit 1
 DosExit 256

section .data
predefined_str:db "Using string with predefined label",10,0
