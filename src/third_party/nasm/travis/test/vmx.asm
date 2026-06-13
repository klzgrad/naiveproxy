[bits 64]

	clgi
	stgi
	vmcall
	vmclear		[0]
	vmfunc
	vmlaunch
	vmload
	vmmcall
	vmptrld		[0x11111111]
	vmptrst		[0x22222222]
	vmread		[0x22222222], rax
	vmresume
	vmrun
	vmsave
	vmwrite		rax, [0x22222222]
	vmxoff
	vmxon		[0x33333333]
	invept		rbx, [0x44444444]
	invvpid		rcx, [0x55555555]

	pvalidate
	rmpadjust

	vmgexit
	repne vmcall
	rep vmcall
