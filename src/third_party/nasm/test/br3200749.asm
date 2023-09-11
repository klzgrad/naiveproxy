;Testname=unoptimized; Arguments=-O0 -fbin -obr3200749.bin; Files=stdout stderr br3200749.bin
;Testname=optimized;   Arguments=-Ox -fbin -obr3200749.bin; Files=stdout stderr br3200749.bin
%define IFNDEF %ifndef
%define ENDIF %endif

IFNDEF foo
    ; bar
ENDIF

