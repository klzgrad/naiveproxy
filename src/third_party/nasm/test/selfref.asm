	bits 32

%idefine zoom $%?
	mov ebx,Zoom
%idefine boom $%?
	mov ecx,Boom

%imacro Foo1 0
	%idefine Bar1 _%?
	%idefine baz1 $%?
	mov BAR1,baz1
%endmacro

	foo1
	mov eax,bar1

%imacro Foo2 0
	%idefine Bar2 _%*?
	%idefine baz2 $%*?
	mov BAR2,baz2
%endmacro

	foo2
	mov eax,bar2
