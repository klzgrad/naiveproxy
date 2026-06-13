;Testname=bin; Arguments=-fbin -o_file_.bin; Files=stdout stderr _file_.bin
	db __FILE__, `\r\n`
	db __FILE__, `\r\n`
	dw __LINE__
	dw __LINE__
