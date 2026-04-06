;Test for bug report 560575 - Using SEG with non-relocatable values doesn't work
;
	dw seg ~1
	dw seg "a"
	dw seg 'a'
