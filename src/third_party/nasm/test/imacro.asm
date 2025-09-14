;Testname=test; Arguments=-fbin -oimacro.bin; Files=stdout stderr imacro.bin

%imacro Zero 1
	xor %1,%1
%endmacro

	Zero eax
	zero eax
