	bits 64
	[warning -obsolete]

	bextr rax, rsi, 1
	bextr eax, esi, 1
	bextr eax, esi, eax

	blcfill edx, ebx
	blcfill edx, [ebx]
	blcfill rax, rbx

	blci edx, ebx
	blci edx, [ebx]
	blci rax, rbx

	blcic edx, ebx
	blcic edx, [ebx]
	blcic rax, rbx

	blcmsk edx, ebx
	blcmsk edx, [ebx]
	blcmsk rax, rbx

	blcs edx, ebx
	blcs edx, [ebx]
	blcs rax, rbx

	blsfill edx, ebx
	blsfill edx, [ebx]
	blsfill rax, rbx

	blsic edx, ebx
	blsic edx, [ebx]
	blsic rax, rbx

	t1mskc edx, ebx
	t1mskc edx, [ebx]
	t1mskc rax, rbx

	tzmsk edx, ebx
	tzmsk edx, [ebx]
	tzmsk rax, rbx
