;Testname=test; Arguments=-fbin -osmartalign16.bin; Files=stdout stderr smartalign16.bin

%use smartalign

	bits 16

	alignmode nop, 32
	add ax,ax
	align 32

	alignmode generic, 32
	add ax,ax
	align 32

	alignmode k7, 32
	add ax,ax
	align 32

	alignmode k8, 32
	add ax,ax
	align 32

	alignmode p6, 32
	add ax,ax
	align 32

	add ecx,ecx
	align 32
	add edx,edx
	align 128
	add ebx,ebx
	align 256
	add esi,esi
	align 512

	add edi,edi
