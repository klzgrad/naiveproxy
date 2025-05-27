bits 64
default rel

%use smartalign

section	.text code align=32

align 32

nop
jz  LDone

%rep 10
	nop
%endrep

align 16
%rep 115
	nop
%endrep

LDone:
