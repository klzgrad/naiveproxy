;Testname=aout;  Arguments=-faout  -obr560575.o; Files=stderr stdout br560575.o
;Testname=aoutb; Arguments=-faoutb -obr560575.o; Files=stderr stdout br560575.o
;Testname=coff;  Arguments=-fcoff  -obr560575.o; Files=stderr stdout br560575.o
;Testname=elf32; Arguments=-felf32 -obr560575.o; Files=stderr stdout br560575.o
;Testname=elf64; Arguments=-felf64 -obr560575.o; Files=stderr stdout br560575.o
;Testname=as86;  Arguments=-fas86  -obr560575.o; Files=stderr stdout br560575.o
;Testname=win32; Arguments=-fwin32 -obr560575.o; Files=stderr stdout br560575.o
;Testname=win64; Arguments=-fwin64 -obr560575.o; Files=stderr stdout br560575.o
;Testname=rdf;   Arguments=-frdf   -obr560575.o; Files=stderr stdout br560575.o
;Testname=ieee;  Arguments=-fieee  -obr560575.o; Files=stderr stdout br560575.o
;Testname=macho; Arguments=-fmacho -obr560575.o; Files=stderr stdout br560575.o

;Test for bug report 560575 - Using SEG with non-relocatable values doesn't work
;
	dw seg ~1
	dw seg "a"
	dw seg 'a'
