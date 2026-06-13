;Testname=test; Arguments=-fbin -ozerobyte.bin; Files=stdout stderr zerobyte.bin
	bits 64

	mov eax,bar-foo

foo:
	add al,r10b
bar:

	lldt ax
	lldt r8w
	ltr [rax]
	sldt eax
	sldt r8d
	str eax
	str rax
	str r8d
	str r8
	verr ax
	verr r8w
	verw ax
	verw r8w
