;Testname=br3028880; Arguments=-Ox -fbin -obr3028880.o; Files=stdout stderr br3028880.o

%macro import 1
	%define %%incfile %!PROJECTBASEDIR/%{1}.inc
%endmacro

import foo

