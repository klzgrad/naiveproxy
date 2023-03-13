;Testname=O0; Arguments=-O0 -fbin -ooptimization.bin; Files=stdout stderr optimization.bin
;Testname=O1; Arguments=-O1 -fbin -ooptimization.bin; Files=stdout stderr optimization.bin
;Testname=Ox; Arguments=-Ox -fbin -ooptimization.bin; Files=stdout stderr optimization.bin

BITS 32

; Simple
jmp foo
times 124 nop
foo:

; Must start short to converge optimally
jmp car
times 127 nop
car:

; Always near
jmp cdr
times 128 nop
cdr:


; Simple
add eax, quux2 - quux1
quux1:
times 127 nop
quux2:

; Must start short
corge1:
add eax, corge2 - corge1
times 127 - 3 nop
corge2:


; Simple
lea eax, [bolug2-bolug1]
bolug1:
times 127 nop
bolug2:

; Must start short
calog1:
lea eax, [calog2-calog1]
times 127 - 3 nop
calog2:


; Simple
lea eax, [eax+dolug2-dolug1]
dolug1:
times 127 nop
dolug2:

; Must start short
ealog1:
lea eax, [eax+ealog2-ealog1]
times 127 - 3 nop
ealog2:

; Must stay long!
lea eax, [eax+folug2-folug1]
folug1:
times 128 nop
folug2:

; Must stay long!
galog1:
lea eax, [eax+galog2-galog1]
times 128 - 3 nop
galog2:

; Sbyte tests...
onetwentysix	equ 126
onetwentynine	equ 129
	
add eax,onetwentyseven		; sbyte (forward)
add eax,onetwentyeight		; not sbyte (forward)
add eax,onetwentyseven		; sbyte (forward)
add eax,onetwentysix		; sbyte (backward)
add eax,onetwentynine		; not sbyte (backward)
add ecx,onetwentyseven		; sbyte (forward)
add ecx,onetwentyeight		; not sbyte (forward)
add ecx,onetwentyseven		; sbyte (forward)
add ecx,onetwentysix		; sbyte (backward)
add ecx,onetwentynine		; not sbyte (backward)
	
onetwentyseven	equ 127
onetwentyeight	equ 128

; Simple
add eax, holug2-holug1
holug1:
times 127 nop
holug2:

; Must start short
ialog1:
add eax, ialog2-ialog1
times 127 - 3 nop
ialog2:
	
; Do not confuse forward references and segmentless addresses!
jmp 12345
