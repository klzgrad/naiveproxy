;Testname=obj; Arguments=-fobj -onasmfomat.o; Files=stdout stderr nasmfomat.o
;Testname=bin; Arguments=-fbin -onasmfomat.o; Files=stdout stderr nasmfomat.o

%if __OUTPUT_FORMAT__ == 'bin'

db 'This is binary format file'

%elif __OUTPUT_FORMAT__ == 'obj'

db 'This is object format file'

%else

db 'This is some other format file'

%endif
