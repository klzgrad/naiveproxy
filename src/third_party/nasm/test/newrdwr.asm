;Testname=test; Arguments=-fbin -onewrdwr.bin; Files=stdout stderr newrdwr.bin

	bits 64

	rdfsbase eax
	rdfsbase rax
	rdgsbase eax
	rdgsbase rax
	rdrand ax
	rdrand eax
	rdrand rax
	wrfsbase eax
	wrfsbase rax
	wrgsbase eax
	wrgsbase rax

	osp rdfsbase eax
	osp rdfsbase rax
	osp rdgsbase eax
	osp rdgsbase rax
	osp wrfsbase eax
	osp wrfsbase rax
	osp wrgsbase eax
	osp wrgsbase rax
