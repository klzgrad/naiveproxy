	default rel
%use ifunc

%define BREG %selbits(bx,ebx,rbx)
%define OREG %selbits(ebx,bx,ebx) ; Force a 67 prefix

	lea BREG,lbl
	lgdt [BREG]
	sgdt [BREG]
	lidt [BREG]
	sidt [BREG]

	lgdt [OREG]
	sgdt [OREG]
	lidt [OREG]
	sidt [OREG]

	lgdt [lbl]
	sgdt [lbl]
	lidt [lbl]
	sidt [lbl]

	hlt
lbl:
	times 10 nop
