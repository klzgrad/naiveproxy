;Testname=unoptimized; Arguments=-fbin -oinvlpga.bin;     Files=stdout stderr invlpga.bin
;Testname=optimized;   Arguments=-fbin -oinvlpga.bin -Ox; Files=stdout stderr invlpga.bin

	bits 32
	invlpga
	invlpga ax,ecx
	invlpga eax,ecx
	bits 64
	invlpga
	invlpga eax,ecx
	invlpga rax,ecx
