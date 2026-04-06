;
; ifmacro.asm
;
; Test of the new ifmacro directive
;
; This file produces a human-readable text file when compiled
; with -f bin
;

%define LF 10

%macro dummy 2
	db 'This is a dummy macro, '
	db 'arg1 = ', %1, ', '
	db 'arg2 = ', %2, LF
%endmacro

	dummy 'this', 'that'

%ifdef CR
	db '%ifdef CR', LF
%endif

%ifdef LF
	db '%ifdef LF', LF
%endif

%ifmacro dummy 1
	db '%ifmacro dummy 1', LF
%endif

%ifmacro dummy 2
	db '%ifmacro dummy 2', LF
%endif

%ifmacro dummy 3
	db '%ifmacro dummy 3', LF
%endif

%ifmacro dummy 1+
	db '%ifmacro dummy 1+', LF
%endif

%ifmacro dummy 2+
	db '%ifmacro dummy 2+', LF
%endif

%ifmacro dummy 3+
	db '%ifmacro dummy 3+', LF
%endif

%ifmacro dummy
	db '%ifmacro dummy', LF
%endif

%ifmacro dummy 0-1
	db '%ifmacro dummy 0-1', LF
%endif

%ifmacro dummy 1-2
	db '%ifmacro dummy 1-2', LF
%endif

%ifmacro dummy 2-3
	db '%ifmacro dummy 2-3', LF
%endif

%ifmacro dummy 3-4
	db '%ifmacro dummy 3-4', LF
%endif

%ifmacro LF
	db '%ifmacro LF', LF
%endif

%ifndef CR
	db '%ifndef CR', LF
%endif

%ifndef LF
	db '%ifndef LF', LF
%endif

%ifnmacro dummy 1
	db '%ifnmacro dummy 1', LF
%endif

%ifnmacro dummy 2
	db '%ifnmacro dummy 2', LF
%endif

%ifnmacro dummy 3
	db '%ifnmacro dummy 3', LF
%endif

%ifnmacro dummy 1+
	db '%ifnmacro dummy 1+', LF
%endif

%ifnmacro dummy 2+
	db '%ifnmacro dummy 2+', LF
%endif

%ifnmacro dummy 3+
	db '%ifnmacro dummy 3+', LF
%endif

%ifnmacro dummy
	db '%ifnmacro dummy', LF
%endif

%ifnmacro dummy 0-1
	db '%ifnmacro dummy 0-1', LF
%endif

%ifnmacro dummy 1-2
	db '%ifnmacro dummy 1-2', LF
%endif

%ifnmacro dummy 2-3
	db '%ifnmacro dummy 2-3', LF
%endif

%ifnmacro dummy 3-4
	db '%ifnmacro dummy 3-4', LF
%endif

%ifnmacro LF
	db '%ifnmacro LF', LF
%endif

%if 0
%elifdef CR
	db '%elifdef CR', CR
%endif

%if 0
%elifdef LF
	db '%elifdef LF', LF
%endif

%if 0
%elifmacro dummy 1
	db '%elifmacro dummy 1', LF
%endif

%if 0
%elifmacro dummy 2
	db '%elifmacro dummy 2', LF
%endif

%if 0
%elifmacro dummy 3
	db '%elifmacro dummy 3', LF
%endif

%if 0
%elifmacro dummy 1+
	db '%elifmacro dummy 1+', LF
%endif

%if 0
%elifmacro dummy 2+
	db '%elifmacro dummy 2+', LF
%endif

%if 0
%elifmacro dummy 3+
	db '%elifmacro dummy 3+', LF
%endif

%if 0
%elifmacro dummy
	db '%elifmacro dummy', LF
%endif

%if 0
%elifmacro dummy 0-1
	db '%elifmacro dummy 0-1', LF
%endif

%if 0
%elifmacro dummy 1-2
	db '%elifmacro dummy 1-2', LF
%endif

%if 0
%elifmacro dummy 2-3
	db '%elifmacro dummy 2-3', LF
%endif

%if 0
%elifmacro dummy 3-4
	db '%elifmacro dummy 3-4', LF
%endif

%if 0
%elifmacro LF
	db '%elifmacro LF', LF
%endif

%if 0
%elifndef CR
	db '%elifndef CR', LF
%endif

%if 0
%elifndef LF
	db '%elifndef LF', LF
%endif

%if 0
%elifnmacro dummy 1
	db '%elifnmacro dummy 1', LF
%endif

%if 0
%elifnmacro dummy 2
	db '%elifnmacro dummy 2', LF
%endif

%if 0
%elifnmacro dummy 3
	db '%elifnmacro dummy 3', LF
%endif

%if 0
%elifnmacro dummy 1+
	db '%elifnmacro dummy 1+', LF
%endif

%if 0
%elifnmacro dummy 2+
	db '%elifnmacro dummy 2+', LF
%endif

%if 0
%elifnmacro dummy 3+
	db '%elifnmacro dummy 3+', LF
%endif

%if 0
%elifnmacro dummy
	db '%elifnmacro dummy', LF
%endif

%if 0
%elifnmacro dummy 0-1
	db '%elifnmacro dummy 0-1', LF
%endif

%if 0
%elifnmacro dummy 1-2
	db '%elifnmacro dummy 1-2', LF
%endif

%if 0
%elifnmacro dummy 2-3
	db '%elifnmacro dummy 2-3', LF
%endif

%if 0
%elifnmacro dummy 3-4
	db '%elifnmacro dummy 3-4', LF
%endif

%if 0
%elifnmacro LF
	db '%elifnmacro LF', LF
%endif

%if 1
%elifdef CR
	db 'bad %elifdef CR', LF
%endif

%if 1
%elifdef LF
	db 'bad %elifdef LF', LF
%endif

%if 1
%elifmacro dummy 1
	db 'bad %elifmacro dummy 1', LF
%endif

%if 1
%elifmacro dummy 2
	db 'bad %elifmacro dummy 2', LF
%endif

%if 1
%elifmacro dummy 3
	db 'bad %elifmacro dummy 3', LF
%endif

%if 1
%elifmacro dummy 1+
	db 'bad %elifmacro dummy 1+', LF
%endif

%if 1
%elifmacro dummy 2+
	db 'bad %elifmacro dummy 2+', LF
%endif

%if 1
%elifmacro dummy 3+
	db 'bad %elifmacro dummy 3+', LF
%endif

%if 1
%elifmacro dummy
	db 'bad %elifmacro dummy', LF
%endif

%if 1
%elifmacro dummy 0-1
	db 'bad %elifmacro dummy 0-1', LF
%endif

%if 1
%elifmacro dummy 1-2
	db 'bad %elifmacro dummy 1-2', LF
%endif

%if 1
%elifmacro dummy 2-3
	db 'bad %elifmacro dummy 2-3', LF
%endif

%if 1
%elifmacro dummy 3-4
	db 'bad %elifmacro dummy 3-4', LF
%endif

%if 1
%elifmacro LF
	db 'bad %elifmacro LF', LF
%endif

%if 1
%elifndef CR
	db 'bad %elifndef CR', LF
%endif

%if 1
%elifndef LF
	db 'bad %elifndef LF', LF
%endif

%if 1
%elifnmacro dummy 1
	db 'bad %elifnmacro dummy 1', LF
%endif

%if 1
%elifnmacro dummy 2
	db 'bad %elifnmacro dummy 2', LF
%endif

%if 1
%elifnmacro dummy 3
	db 'bad %elifnmacro dummy 3', LF
%endif

%if 1
%elifnmacro dummy 1+
	db 'bad %elifnmacro dummy 1+', LF
%endif

%if 1
%elifnmacro dummy 2+
	db 'bad %elifnmacro dummy 2+', LF
%endif

%if 1
%elifnmacro dummy 3+
	db 'bad %elifnmacro dummy 3+', LF
%endif

%if 1
%elifnmacro dummy
	db 'bad %elifnmacro dummy', LF
%endif

%if 1
%elifnmacro dummy 0-1
	db 'bad %elifnmacro dummy 0-1', LF
%endif

%if 1
%elifnmacro dummy 1-2
	db 'bad %elifnmacro dummy 1-2', LF
%endif

%if 1
%elifnmacro dummy 2-3
	db 'bad %elifnmacro dummy 2-3', LF
%endif

%if 1
%elifnmacro dummy 3-4
	db 'bad %elifnmacro dummy 3-4', LF
%endif

%if 1
%elifnmacro LF
	db 'bad %elifnmacro LF', LF
%endif

